// WP03-T004-B: GoalContract boundary contract tests.
//
// Validates the WP03-T004 semantic boundary enforced by:
//   - validate_goal_contract_required_fields()  (Layer 1)
//   - validate_goal_contract_boundary()          (Layer 2)
//
// Test coverage:
//   Positive: 4 scenarios proving valid GoalContract states.
//   Negative: 14 scenarios covering missing required fields, enum range
//             violations, and boundary constraint violations.

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "agent/GoalContract.h"
#include "agent/GoalContractGuards.h"
#include "checkpoint/RuntimeBudget.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

using dasall::contracts::ApprovalPolicy;
using dasall::contracts::GoalContract;
using dasall::contracts::GoalContractGuardResult;
using dasall::contracts::GoalStatus;
using dasall::contracts::RuntimeBudget;
using dasall::contracts::validate_goal_contract_boundary;
using dasall::contracts::validate_goal_contract_required_fields;
using dasall::tests::support::assert_true;

// ---------------------------------------------------------------------------
// Helper: construct a valid minimal GoalContract with all required fields set.
// ---------------------------------------------------------------------------

GoalContract make_valid_goal() {
  GoalContract goal;
  goal.goal_id = "goal-001";
  goal.request_id = "req-001";
  goal.goal_description = "Summarize the weekly engineering report";
  goal.success_criteria = R"({"type":"summary","min_sections":3})";
  goal.status = GoalStatus::Active;
  goal.created_at = 1710000000000;
  return goal;
}

// ===========================================================================
// Positive cases
// ===========================================================================

// P1: Minimal valid goal (required fields only).
void test_minimal_valid_goal_passes_required_fields() {
  auto goal = make_valid_goal();
  auto result = validate_goal_contract_required_fields(goal);
  assert_true(result.ok,
              "minimal valid goal should pass required fields guard");
}

// P2: Minimal valid goal passes boundary guard.
void test_minimal_valid_goal_passes_boundary() {
  auto goal = make_valid_goal();
  auto result = validate_goal_contract_boundary(goal);
  assert_true(result.ok,
              "minimal valid goal should pass boundary guard");
}

// P3: Full goal with all optional fields set.
void test_full_valid_goal_passes_boundary() {
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
  goal.budget_override = budget;

  auto result = validate_goal_contract_boundary(goal);
  assert_true(result.ok,
              "full valid goal with all optional fields should pass");
}

// P4: All GoalStatus values (except Unspecified) accepted.
void test_all_valid_goal_statuses_accepted() {
  auto goal = make_valid_goal();

  goal.status = GoalStatus::Active;
  assert_true(validate_goal_contract_boundary(goal).ok,
              "Active status should be accepted");

  goal.status = GoalStatus::Achieved;
  assert_true(validate_goal_contract_boundary(goal).ok,
              "Achieved status should be accepted");

  goal.status = GoalStatus::Failed;
  assert_true(validate_goal_contract_boundary(goal).ok,
              "Failed status should be accepted");

  goal.status = GoalStatus::Cancelled;
  assert_true(validate_goal_contract_boundary(goal).ok,
              "Cancelled status should be accepted");
}

// ===========================================================================
// Negative cases: missing required fields
// ===========================================================================

// N1: Missing goal_id.
void test_missing_goal_id_fails() {
  auto goal = make_valid_goal();
  goal.goal_id = std::nullopt;
  auto result = validate_goal_contract_required_fields(goal);
  assert_true(!result.ok, "missing goal_id should fail");
}

// N2: Empty goal_id.
void test_empty_goal_id_fails() {
  auto goal = make_valid_goal();
  goal.goal_id = "";
  auto result = validate_goal_contract_required_fields(goal);
  assert_true(!result.ok, "empty goal_id should fail");
}

// N3: Missing request_id.
void test_missing_request_id_fails() {
  auto goal = make_valid_goal();
  goal.request_id = std::nullopt;
  auto result = validate_goal_contract_required_fields(goal);
  assert_true(!result.ok, "missing request_id should fail");
}

// N4: Missing goal_description.
void test_missing_goal_description_fails() {
  auto goal = make_valid_goal();
  goal.goal_description = std::nullopt;
  auto result = validate_goal_contract_required_fields(goal);
  assert_true(!result.ok, "missing goal_description should fail");
}

// N5: Empty goal_description.
void test_empty_goal_description_fails() {
  auto goal = make_valid_goal();
  goal.goal_description = "";
  auto result = validate_goal_contract_required_fields(goal);
  assert_true(!result.ok, "empty goal_description should fail");
}

// N6: Missing success_criteria.
void test_missing_success_criteria_fails() {
  auto goal = make_valid_goal();
  goal.success_criteria = std::nullopt;
  auto result = validate_goal_contract_required_fields(goal);
  assert_true(!result.ok, "missing success_criteria should fail");
}

// N7: Empty success_criteria.
void test_empty_success_criteria_fails() {
  auto goal = make_valid_goal();
  goal.success_criteria = "";
  auto result = validate_goal_contract_required_fields(goal);
  assert_true(!result.ok, "empty success_criteria should fail");
}

// N8: Unspecified status.
void test_unspecified_status_fails() {
  auto goal = make_valid_goal();
  goal.status = GoalStatus::Unspecified;
  auto result = validate_goal_contract_required_fields(goal);
  assert_true(!result.ok, "Unspecified status should fail");
}

// N9: Missing status.
void test_missing_status_fails() {
  auto goal = make_valid_goal();
  goal.status = std::nullopt;
  auto result = validate_goal_contract_required_fields(goal);
  assert_true(!result.ok, "missing status should fail");
}

// N10: Missing created_at.
void test_missing_created_at_fails() {
  auto goal = make_valid_goal();
  goal.created_at = std::nullopt;
  auto result = validate_goal_contract_required_fields(goal);
  assert_true(!result.ok, "missing created_at should fail");
}

// N11: Zero created_at.
void test_zero_created_at_fails() {
  auto goal = make_valid_goal();
  goal.created_at = 0;
  auto result = validate_goal_contract_required_fields(goal);
  assert_true(!result.ok, "zero created_at should fail");
}

// ===========================================================================
// Negative cases: boundary violations
// ===========================================================================

// N12: deadline_at before created_at.
void test_deadline_before_created_at_fails() {
  auto goal = make_valid_goal();
  goal.deadline_at = 1709999999999;  // earlier than created_at
  auto result = validate_goal_contract_boundary(goal);
  assert_true(!result.ok, "deadline_at before created_at should fail");
}

// N13: Negative deadline_at.
void test_negative_deadline_fails() {
  auto goal = make_valid_goal();
  goal.deadline_at = -1;
  auto result = validate_goal_contract_boundary(goal);
  assert_true(!result.ok, "negative deadline_at should fail");
}

// N14: Zero priority.
void test_zero_priority_fails() {
  auto goal = make_valid_goal();
  goal.priority = 0;
  auto result = validate_goal_contract_boundary(goal);
  assert_true(!result.ok, "zero priority should fail");
}

}  // namespace

int main() {
  try {
    // Positive cases (4)
    test_minimal_valid_goal_passes_required_fields();
    test_minimal_valid_goal_passes_boundary();
    test_full_valid_goal_passes_boundary();
    test_all_valid_goal_statuses_accepted();

    // Negative cases: missing required fields (11)
    test_missing_goal_id_fails();
    test_empty_goal_id_fails();
    test_missing_request_id_fails();
    test_missing_goal_description_fails();
    test_empty_goal_description_fails();
    test_missing_success_criteria_fails();
    test_empty_success_criteria_fails();
    test_unspecified_status_fails();
    test_missing_status_fails();
    test_missing_created_at_fails();
    test_zero_created_at_fails();

    // Negative cases: boundary violations (3)
    test_deadline_before_created_at_fails();
    test_negative_deadline_fails();
    test_zero_priority_fails();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
