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
    test_validate_stage_output_rejects_missing_enum_numeric_and_list_violations();
    test_validate_action_decision_invariants_rejects_missing_execute_action_fields();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}