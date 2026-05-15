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
using dasall::cognition::validation::schema_for_planning_plan;
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
  assert_true(contains_string(schema.required_fields, "nodes"),
              "planning schema should require nodes");
  assert_true(has_enum_constraint(schema, "nodes.action_kind_hint", "tool_action"),
              "planning schema should constrain node action kinds");
  assert_true(schema.unknown_field_policy == UnknownFieldPolicy::AllowRegisteredExtensions,
              "planning schema should only allow registered extensions");
  assert_true(contains_string(schema.allowed_extension_prefixes, "x_"),
              "planning schema should only allow x_ extension fields");
}

void test_execution_schema_registry_freezes_action_baseline() {
  const auto& schema = schema_for_execution_action_decision();

  assert_equal(std::string("execution"), schema.stage_name,
               "execution schema should declare the execution stage owner");
  assert_equal(std::string("cognition.reasoning.v1"), schema.schema_version,
               "execution schema should freeze cognition.reasoning.v1");
  assert_true(contains_string(schema.required_fields, "decision_kind"),
              "execution schema should require decision_kind");
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

}  // namespace

int main() {
  try {
    test_planning_schema_registry_freezes_plan_baseline();
    test_execution_schema_registry_freezes_action_baseline();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}