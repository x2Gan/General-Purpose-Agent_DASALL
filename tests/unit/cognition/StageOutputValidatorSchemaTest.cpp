#include <exception>
#include <iostream>
#include <string>

#include "validation/StageOutputValidator.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::decision::ActionDecision;
using dasall::cognition::decision::ActionDecisionKind;
using dasall::cognition::decision::ToolIntentHint;
using dasall::cognition::llm_bridge::StageBudgetHint;
using dasall::cognition::llm_bridge::StageLlmCallResult;
using dasall::cognition::validation::EnumConstraint;
using dasall::cognition::validation::ListConstraint;
using dasall::cognition::validation::NumericConstraint;
using dasall::cognition::validation::StageOutputValidator;
using dasall::cognition::validation::StageSchemaSpec;
using dasall::cognition::validation::ValidationIssueCode;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] StageLlmCallResult make_stage_result(std::string payload) {
  dasall::contracts::LLMResponse response;
  response.response_kind = dasall::contracts::LLMResponseKind::DirectResponse;
  response.content_payload = std::move(payload);

  return StageLlmCallResult{
      .response = std::move(response),
      .error_info = std::nullopt,
      .result_code = std::nullopt,
      .budget_hint = StageBudgetHint{},
      .resolved_route = "llm.exec.primary",
      .warnings = {},
      .diagnostics = {},
      .fallback_used = false,
  };
}

[[nodiscard]] StageSchemaSpec make_schema_spec() {
  return StageSchemaSpec{
      .stage_name = "execution",
      .required_fields = {"decision_kind", "confidence", "candidate_scores"},
      .enum_constraints = {
          EnumConstraint{.field_path = "decision_kind",
                         .allowed_values = {"ExecuteAction", "DirectResponse"}},
      },
      .numeric_bounds = {
          NumericConstraint{.field_path = "confidence", .min_value = 0.0, .max_value = 1.0},
      },
      .list_constraints = {
          ListConstraint{.field_path = "candidate_scores", .min_items = 1U, .max_items = 4U},
      },
      .stage_specific_invariants = {},
  };
}

[[nodiscard]] bool has_issue_code(const dasall::cognition::validation::ValidationResult& result,
                                  ValidationIssueCode code,
                                  const std::string& field_path) {
  for (const auto& issue : result.issue_set.issues) {
    if (issue.code == code && issue.field_path == field_path) {
      return true;
    }
  }
  return false;
}

void test_validate_stage_output_accepts_well_formed_payload() {
  StageOutputValidator validator;
  const auto result = validator.validate_stage_output(
      make_stage_result(
          R"({"decision_kind":"ExecuteAction","confidence":0.82,"candidate_scores":["execute_action"]})"),
      make_schema_spec());

  assert_true(result.ok, "well-formed stage output should pass schema validation");
  assert_true(result.issue_set.empty(),
              "well-formed stage output should not emit validation issues");
}

void test_validate_stage_output_accepts_whitespace_and_reordered_fields() {
  StageOutputValidator validator;
  const auto result = validator.validate_stage_output(
      make_stage_result(
          "{\n"
          "  \"candidate_scores\" : [ \"execute_action\", \"fallback\" ],\n"
          "  \"confidence\" : 0.82,\n"
          "  \"decision_kind\" : \"ExecuteAction\"\n"
          "}"),
      make_schema_spec());

  assert_true(result.ok, "whitespace and field order variation should remain valid JSON");
  assert_true(result.issue_set.empty(),
              "whitespace and field order variation should not emit validation issues");
}

void test_validate_stage_output_rejects_missing_enum_numeric_and_list_violations() {
  StageOutputValidator validator;
  const auto result = validator.validate_stage_output(
      make_stage_result(
          R"({"decision_kind":"LaunchAction","confidence":1.40,"candidate_scores":[]})"),
      make_schema_spec());

  assert_true(!result.ok, "invalid stage output should fail schema validation");
  assert_equal(3, static_cast<int>(result.issue_set.issues.size()),
               "invalid payload should emit enum, numeric and list issues");
  assert_true(result.error_info.has_value(),
              "invalid stage output should emit an ErrorInfo payload");
  assert_equal(std::string("execution"), result.error_info->details.stage,
               "schema failures should be attributed to the validated stage");
}

  void test_validate_stage_output_rejects_escaped_pseudo_fields() {
    StageOutputValidator validator;
    const auto result = validator.validate_stage_output(
      make_stage_result(
        R"({"confidence":0.82,"candidate_scores":["execute_action"],"note":"\"decision_kind\":\"ExecuteAction\""})"),
      make_schema_spec());

    assert_true(!result.ok, "escaped pseudo-fields inside strings must not satisfy required fields");
    assert_true(has_issue_code(result,
                 ValidationIssueCode::MissingRequiredField,
                 "decision_kind"),
          "missing decision_kind should be reported even when a string literal contains a pseudo-field");
  }

  void test_validate_stage_output_counts_only_top_level_array_items() {
    StageOutputValidator validator;
    const auto result = validator.validate_stage_output(
      make_stage_result(
        R"({"decision_kind":"ExecuteAction","confidence":0.82,"candidate_scores":[[1,2],[3,4]]})"),
      make_schema_spec());

    assert_true(result.ok, "nested array members should count as two top-level list items, not four");
    assert_true(result.issue_set.empty(),
          "top-level list item counting should not emit validation issues for nested arrays");
  }

  void test_validate_stage_output_rejects_malformed_json() {
    StageOutputValidator validator;
    const auto result = validator.validate_stage_output(
      make_stage_result(
        R"({"decision_kind":"ExecuteAction","confidence":0.82,"candidate_scores":["execute_action"] )"),
      make_schema_spec());

    assert_true(!result.ok, "malformed JSON must fail closed");
    assert_equal(1, static_cast<int>(result.issue_set.issues.size()),
           "malformed JSON should stop at a single malformed-json issue");
    assert_true(result.issue_set.issues.front().code == ValidationIssueCode::MalformedJson,
          "malformed payloads must surface the malformed-json issue code");
  }

  void test_validate_stage_output_rejects_field_type_mismatches() {
    StageOutputValidator validator;
    const auto result = validator.validate_stage_output(
      make_stage_result(
        R"({"decision_kind":"ExecuteAction","confidence":"0.82","candidate_scores":{"primary":"execute_action"}})"),
      make_schema_spec());

    assert_true(!result.ok, "schema type mismatches must fail validation");
    assert_true(has_issue_code(result,
                 ValidationIssueCode::NumericOutOfRange,
                 "confidence"),
          "string confidence should surface a numeric validation issue");
    assert_true(has_issue_code(result,
                 ValidationIssueCode::ListSizeOutOfRange,
                 "candidate_scores"),
          "object candidate_scores should surface a list validation issue");
  }

void test_validate_action_decision_invariants_rejects_missing_execute_action_fields() {
  StageOutputValidator validator;
  ActionDecision action_decision;
  action_decision.decision_kind = ActionDecisionKind::ExecuteAction;
  action_decision.confidence = 0.75F;
  action_decision.tool_intent_hint = ToolIntentHint{};

  const auto result = validator.validate_action_decision_invariants(action_decision);

  assert_true(!result.ok, "execute_action without node id or tool name must fail invariants");
  assert_equal(2, static_cast<int>(result.issue_set.issues.size()),
               "missing execute_action fields should surface two invariant issues");
  assert_true(result.issue_set.issues.front().code == ValidationIssueCode::ActionDecisionInvariant,
              "action decision failures must use the action-decision invariant code");
}

}  // namespace

int main() {
  try {
    test_validate_stage_output_accepts_well_formed_payload();
    test_validate_stage_output_accepts_whitespace_and_reordered_fields();
    test_validate_stage_output_rejects_missing_enum_numeric_and_list_violations();
    test_validate_stage_output_rejects_escaped_pseudo_fields();
    test_validate_stage_output_counts_only_top_level_array_items();
    test_validate_stage_output_rejects_malformed_json();
    test_validate_stage_output_rejects_field_type_mismatches();
    test_validate_action_decision_invariants_rejects_missing_execute_action_fields();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}