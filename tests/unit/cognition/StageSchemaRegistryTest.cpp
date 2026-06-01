#include <algorithm>
#include <exception>
#include <iostream>
#include <string>

#include "support/TestAssertions.h"
#include "validation/StageSchemaRegistry.h"

namespace {

using dasall::cognition::validation::StageSchemaSpec;
using dasall::cognition::validation::UnknownFieldPolicy;
using dasall::cognition::validation::schema_for_execution_action_decision;
using dasall::cognition::validation::schema_for_perception_result;
using dasall::cognition::validation::schema_for_planning_plan;
using dasall::cognition::validation::schema_for_reflection_decision;
using dasall::cognition::validation::schema_for_response_envelope;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] bool contains_string(const std::vector<std::string>& values,
                                   const std::string& expected) {
  return std::find(values.begin(), values.end(), expected) != values.end();
}

[[nodiscard]] bool has_enum_constraint(const StageSchemaSpec& schema,
                                       const std::string& field_path,
                                       const std::string& value) {
  const auto constraint_it = std::find_if(
      schema.enum_constraints.begin(),
      schema.enum_constraints.end(),
      [&](const auto& constraint) { return constraint.field_path == field_path; });
  if (constraint_it == schema.enum_constraints.end()) {
    return false;
  }
  return contains_string(constraint_it->allowed_values, value);
}

void test_planning_schema_registry_freezes_plan_baseline() {
  const auto& schema = schema_for_planning_plan();

  assert_equal(std::string("planning"), schema.stage_name,
               "planning schema should declare the planning stage owner");
  assert_equal(std::string("cognition.plan.v1"), schema.schema_version,
               "planning schema should freeze cognition.plan.v1");
  assert_true(contains_string(schema.required_fields, "plan_id"),
              "planning schema should require plan_id");
  assert_true(contains_string(schema.known_top_level_fields, "open_questions"),
              "planning schema should freeze optional top-level fields for unknown-field checks");
  assert_true(contains_string(schema.required_fields, "nodes"),
              "planning schema should require nodes");
  assert_true(has_enum_constraint(schema, "nodes.action_kind_hint", "tool_action"),
              "planning schema should constrain node action kinds");
  assert_true(schema.unknown_field_policy == UnknownFieldPolicy::AllowRegisteredExtensions,
              "planning schema should only allow registered extensions");
  assert_true(contains_string(schema.allowed_extension_prefixes, "x_"),
              "planning schema should only allow x_ extension fields");
}

void test_perception_schema_registry_freezes_result_baseline() {
  const auto& schema = schema_for_perception_result();

  assert_equal(std::string("perception"), schema.stage_name,
               "perception schema should declare the perception stage owner");
  assert_equal(std::string("cognition.perception.v1"), schema.schema_version,
               "perception schema should freeze cognition.perception.v1");
  assert_true(contains_string(schema.required_fields, "intent_summary"),
              "perception schema should require intent_summary");
  assert_true(contains_string(schema.required_fields, "entities"),
              "perception schema should require entities");
  assert_true(contains_string(schema.known_top_level_fields, "constraints_digest"),
              "perception schema should freeze constraints_digest for unknown-field checks");
  assert_true(has_enum_constraint(schema, "task_type", "plan"),
              "perception schema should constrain task_type literals");
  assert_true(has_enum_constraint(schema, "task_type", "action_decision"),
              "perception schema should allow action_decision task routing");
  assert_equal(2, static_cast<int>(schema.numeric_bounds.size()),
               "perception schema should freeze confidence bounds for the top-level perception score and entity confidences");
}

void test_execution_schema_registry_freezes_action_baseline() {
  const auto& schema = schema_for_execution_action_decision();

  assert_equal(std::string("execution"), schema.stage_name,
               "execution schema should declare the execution stage owner");
  assert_equal(std::string("cognition.reasoning.v1"), schema.schema_version,
               "execution schema should freeze cognition.reasoning.v1");
  assert_true(contains_string(schema.required_fields, "decision_kind"),
              "execution schema should require decision_kind");
  assert_true(contains_string(schema.known_top_level_fields, "tool_intent_hint"),
              "execution schema should freeze known top-level fields for unknown-field checks");
  assert_true(contains_string(schema.required_fields, "candidate_scores"),
              "execution schema should require candidate_scores");
  assert_true(has_enum_constraint(schema, "decision_kind", "ExecuteAction"),
              "execution schema should allow ExecuteAction decisions");
  assert_true(has_enum_constraint(schema, "decision_kind", "AskClarification"),
              "execution schema should allow AskClarification decisions");
  assert_equal(1, static_cast<int>(schema.numeric_bounds.size()),
               "execution schema should freeze confidence as the numeric bound");
  assert_equal(1, static_cast<int>(schema.list_constraints.size()),
               "execution schema should freeze candidate_scores list sizing");
}

void test_reflection_schema_registry_freezes_decision_baseline() {
  const auto& schema = schema_for_reflection_decision();

  assert_equal(std::string("reflection"), schema.stage_name,
               "reflection schema should declare the reflection stage owner");
  assert_equal(std::string("cognition.reflection.v1"), schema.schema_version,
               "reflection schema should freeze cognition.reflection.v1");
  assert_true(contains_string(schema.required_fields, "request_id"),
              "reflection schema should require request_id");
  assert_true(contains_string(schema.known_top_level_fields, "relevant_observation_refs"),
              "reflection schema should freeze optional evidence fields for unknown-field checks");
  assert_true(has_enum_constraint(schema, "decision_kind", "Continue"),
              "reflection schema should allow continue decisions");
  assert_true(has_enum_constraint(schema, "decision_kind", "AbortSafe"),
              "reflection schema should allow abort_safe decisions");
  assert_equal(2, static_cast<int>(schema.numeric_bounds.size()),
               "reflection schema should freeze confidence and created_at numeric bounds");
}

void test_response_schema_registry_freezes_envelope_baseline() {
  const auto& schema = schema_for_response_envelope();

  assert_equal(std::string("response"), schema.stage_name,
               "response schema should declare the response stage owner");
  assert_equal(std::string("cognition.response.v1"), schema.schema_version,
               "response schema should freeze cognition.response.v1");
  assert_true(contains_string(schema.required_fields, "response_mode"),
              "response schema should require response_mode");
  assert_true(contains_string(schema.required_fields, "summary_text"),
              "response schema should require summary_text");
  assert_true(contains_string(schema.known_top_level_fields, "fallback_used"),
              "response schema should freeze fallback_used for unknown-field checks");
  assert_true(has_enum_constraint(schema, "response_mode", "llm_bridge"),
              "response schema should allow llm_bridge mode");
  assert_true(has_enum_constraint(schema, "response_mode", "template_fallback"),
              "response schema should allow template_fallback mode");
  assert_true(schema.unknown_field_policy == UnknownFieldPolicy::AllowRegisteredExtensions,
              "response schema should only allow registered extensions");
}

}  // namespace

int main() {
  try {
    test_planning_schema_registry_freezes_plan_baseline();
    test_perception_schema_registry_freezes_result_baseline();
    test_execution_schema_registry_freezes_action_baseline();
    test_reflection_schema_registry_freezes_decision_baseline();
    test_response_schema_registry_freezes_envelope_baseline();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}