// WP03-T005-B: GoalContract field-level contract tests.
//
// Validates the WP03-T005 field rules enforced by
// validate_goal_contract_field_rules():
//   - Optional string fields (constraints, parent_goal_id) must be non-empty
//     when present.
//   - tags must contain no empty strings and be non-empty when present.
//   - budget_override dimensions must each be > 0 when present.
//   - All required + boundary rules (T004-B) are inherited.

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "agent/GoalContract.h"
#include "agent/GoalContractGuards.h"
#include "checkpoint/RuntimeBudget.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::ApprovalPolicy;
using dasall::contracts::GoalContract;
using dasall::contracts::GoalContractGuardResult;
using dasall::contracts::GoalStatus;
using dasall::contracts::RuntimeBudget;
using dasall::contracts::validate_goal_contract_field_rules;
using dasall::tests::support::assert_true;

// ---------------------------------------------------------------------------
// Helper: construct a valid minimal GoalContract with all required fields set.
// ---------------------------------------------------------------------------

GoalContract make_valid_goal() {
  GoalContract goal;
  goal.goal_id = "goal-field-001";
  goal.request_id = "req-field-001";
  goal.goal_description = "Summarize the weekly engineering report";
  goal.success_criteria = R"({"type":"summary","min_sections":3})";
  goal.status = GoalStatus::Active;
  goal.created_at = 1710000000000;
  return goal;
}

// ===========================================================================
// Positive cases
// ===========================================================================

// P1: Minimal valid goal (required fields only, no optional fields).
void test_minimal_valid_goal_passes_field_rules() {
  auto goal = make_valid_goal();
  auto result = validate_goal_contract_field_rules(goal);
  assert_true(result.ok,
              "minimal valid goal should pass field rules");
}

// P2: Full valid goal with all optional fields properly set.
void test_full_valid_goal_passes_field_rules() {
  auto goal = make_valid_goal();
  goal.constraints = "no-external-api;read-only-db";
  goal.approval_policy = ApprovalPolicy::RequireConfirm;
  goal.priority = 3;
  goal.parent_goal_id = "parent-goal-001";
  goal.deadline_at = 1710000060000;
  goal.tags = std::vector<std::string>{"urgent", "weekly-report"};

  RuntimeBudget budget;
  budget.max_tokens = 4096;
  budget.max_turns = 10;
  budget.max_tool_calls = 20;
  budget.max_latency_ms = 60000;
  budget.max_replan_count = 3;
  goal.budget_override = budget;

  auto result = validate_goal_contract_field_rules(goal);
  assert_true(result.ok,
              "full valid goal with all optional fields should pass");
}

// P3: Valid goal with partial optional fields (proving absence is OK).
void test_partial_optional_fields_passes() {
  auto goal = make_valid_goal();
  goal.constraints = "no-external-api";
  goal.priority = 2;
  // All other optional fields remain nullopt — should pass.
  auto result = validate_goal_contract_field_rules(goal);
  assert_true(result.ok,
              "goal with partial optional fields should pass");
}

// P4: Valid budget_override with only some dimensions set.
void test_partial_budget_override_passes() {
  auto goal = make_valid_goal();
  RuntimeBudget budget;
  budget.max_tokens = 2048;
  // Other dimensions remain nullopt.
  goal.budget_override = budget;
  auto result = validate_goal_contract_field_rules(goal);
  assert_true(result.ok,
              "partial budget_override should pass field rules");
}

// ===========================================================================
// Negative cases: optional string fields present but empty
// ===========================================================================

// N1: Empty constraints.
void test_empty_constraints_fails() {
  auto goal = make_valid_goal();
  goal.constraints = "";
  auto result = validate_goal_contract_field_rules(goal);
  assert_true(!result.ok,
              "empty constraints should fail field rules");
}

// N2: Empty parent_goal_id.
void test_empty_parent_goal_id_fails() {
  auto goal = make_valid_goal();
  goal.parent_goal_id = "";
  auto result = validate_goal_contract_field_rules(goal);
  assert_true(!result.ok,
              "empty parent_goal_id should fail field rules");
}

// ===========================================================================
// Negative cases: tags violations
// ===========================================================================

// N3: Empty tags vector.
void test_empty_tags_vector_fails() {
  auto goal = make_valid_goal();
  goal.tags = std::vector<std::string>{};
  auto result = validate_goal_contract_field_rules(goal);
  assert_true(!result.ok,
              "empty tags vector should fail field rules");
}

// N4: Tags containing an empty string.
void test_tags_with_empty_string_fails() {
  auto goal = make_valid_goal();
  goal.tags = std::vector<std::string>{"valid-tag", ""};
  auto result = validate_goal_contract_field_rules(goal);
  assert_true(!result.ok,
              "tags with empty string should fail field rules");
}

// ===========================================================================
// Negative cases: budget_override dimension violations
// ===========================================================================

// N5: Zero max_tokens in budget_override.
void test_zero_budget_max_tokens_fails() {
  auto goal = make_valid_goal();
  RuntimeBudget budget;
  budget.max_tokens = 0;
  goal.budget_override = budget;
  auto result = validate_goal_contract_field_rules(goal);
  assert_true(!result.ok,
              "zero max_tokens in budget_override should fail field rules");
}

// N6: Zero max_turns in budget_override.
void test_zero_budget_max_turns_fails() {
  auto goal = make_valid_goal();
  RuntimeBudget budget;
  budget.max_turns = 0;
  goal.budget_override = budget;
  auto result = validate_goal_contract_field_rules(goal);
  assert_true(!result.ok,
              "zero max_turns in budget_override should fail field rules");
}

// N7: Zero max_tool_calls in budget_override.
void test_zero_budget_max_tool_calls_fails() {
  auto goal = make_valid_goal();
  RuntimeBudget budget;
  budget.max_tool_calls = 0;
  goal.budget_override = budget;
  auto result = validate_goal_contract_field_rules(goal);
  assert_true(!result.ok,
              "zero max_tool_calls in budget_override should fail field rules");
}

// N8: Zero max_latency_ms in budget_override.
void test_zero_budget_max_latency_ms_fails() {
  auto goal = make_valid_goal();
  RuntimeBudget budget;
  budget.max_latency_ms = 0;
  goal.budget_override = budget;
  auto result = validate_goal_contract_field_rules(goal);
  assert_true(!result.ok,
              "zero max_latency_ms in budget_override should fail field rules");
}

// N9: Zero max_replan_count in budget_override.
void test_zero_budget_max_replan_count_fails() {
  auto goal = make_valid_goal();
  RuntimeBudget budget;
  budget.max_replan_count = 0;
  goal.budget_override = budget;
  auto result = validate_goal_contract_field_rules(goal);
  assert_true(!result.ok,
              "zero max_replan_count in budget_override should fail field rules");
}

// ===========================================================================
// Negative cases: inherited required-field failures (regression guard)
// ===========================================================================

// N10: Missing required field still rejected by field rules.
void test_missing_required_field_still_fails() {
  auto goal = make_valid_goal();
  goal.goal_id = std::nullopt;
  auto result = validate_goal_contract_field_rules(goal);
  assert_true(!result.ok,
              "missing goal_id should still fail via field rules");
}

}  // namespace

int main() {
  try {
    // Positive cases (4)
    test_minimal_valid_goal_passes_field_rules();
    test_full_valid_goal_passes_field_rules();
    test_partial_optional_fields_passes();
    test_partial_budget_override_passes();

    // Negative cases: empty optional strings (2)
    test_empty_constraints_fails();
    test_empty_parent_goal_id_fails();

    // Negative cases: tags violations (2)
    test_empty_tags_vector_fails();
    test_tags_with_empty_string_fails();

    // Negative cases: budget_override dimension violations (5)
    test_zero_budget_max_tokens_fails();
    test_zero_budget_max_turns_fails();
    test_zero_budget_max_tool_calls_fails();
    test_zero_budget_max_latency_ms_fails();
    test_zero_budget_max_replan_count_fails();

    // Negative cases: inherited required-field failure (1)
    test_missing_required_field_still_fails();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
