// ============================================================================
// RecoveryOutcomeFieldContractTest.cpp
//
// WP04-T012-B: Field-level contract test for RecoveryOutcomeGuards.h.
//
// Validates the T012 field-table rules layered on top of the T011 object guard:
//   - String slots must contain non-whitespace content when present.
//   - checkpoint_ref and compensation_result_ref must remain distinct.
//   - ADR-007 failure-attribution fields stay rejected through the existing
//     boundary field-name catalog.
// ============================================================================

#include <exception>
#include <iostream>
#include <string>

#include "checkpoint/RecoveryOutcome.h"
#include "checkpoint/RecoveryOutcomeGuards.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

using dasall::contracts::RecoveryBoundaryDecision;
using dasall::contracts::RecoveryOutcome;
using dasall::contracts::validate_recovery_outcome_contract_field_boundary;
using dasall::contracts::validate_recovery_outcome_field_rules;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

RecoveryOutcome make_valid_outcome() {
  RecoveryOutcome outcome;
  outcome.executed_action = "retry_step";
  outcome.final_runtime_state = "running";
  outcome.updated_retry_count = 3U;
  outcome.checkpoint_ref = "cp-012";
  return outcome;
}

void test_valid_outcome_passes_field_rules() {
  auto outcome = make_valid_outcome();
  outcome.compensation_result_ref = "comp-012";
  outcome.rejection_reason = "retry budget admission denied";

  const auto result = validate_recovery_outcome_field_rules(outcome);
  assert_true(result.ok,
              "valid RecoveryOutcome should pass field rules");
}

void test_boundary_failure_is_inherited() {
  auto outcome = make_valid_outcome();
  outcome.rejection_reason = "denied by runtime policy";
  outcome.escalation_reason = "manual review required";

  const auto result = validate_recovery_outcome_field_rules(outcome);
  assert_true(!result.ok,
              "field-rules guard should inherit boundary failures");
  assert_equal("rejection_reason and escalation_reason must not both be present",
               std::string(result.reason),
               "inherited boundary failure should preserve canonical reason");
}

void test_whitespace_only_executed_action_rejected() {
  auto outcome = make_valid_outcome();
  outcome.executed_action = " \t\n ";

  const auto result = validate_recovery_outcome_field_rules(outcome);
  assert_true(!result.ok,
              "whitespace-only executed_action should be rejected");
  assert_equal(
      "executed_action must contain at least one non-whitespace character",
      std::string(result.reason),
      "whitespace-only executed_action should return canonical reason");
}

void test_whitespace_only_rejection_reason_rejected() {
  auto outcome = make_valid_outcome();
  outcome.rejection_reason = "   ";

  const auto result = validate_recovery_outcome_field_rules(outcome);
  assert_true(!result.ok,
              "whitespace-only rejection_reason should be rejected");
  assert_equal(
      "rejection_reason must contain at least one non-whitespace character when present",
      std::string(result.reason),
      "whitespace-only rejection_reason should return canonical reason");
}

void test_collapsed_control_refs_rejected() {
  auto outcome = make_valid_outcome();
  outcome.compensation_result_ref = "cp-012";

  const auto result = validate_recovery_outcome_field_rules(outcome);
  assert_true(!result.ok,
              "collapsed checkpoint/compensation refs should be rejected");
  assert_equal(
      "checkpoint_ref and compensation_result_ref must not use the same identifier",
      std::string(result.reason),
      "collapsed control refs should return canonical reason");
}

void test_failure_attribution_field_stays_rejected() {
  const auto result =
      validate_recovery_outcome_contract_field_boundary("plan_patch_hint");

  assert_true(!result.allowed,
              "plan_patch_hint must remain rejected for RecoveryOutcome");
  assert_equal(
      static_cast<int>(RecoveryBoundaryDecision::RejectRecoveryAttributionField),
      static_cast<int>(result.decision),
      "failure attribution intrusion should preserve rejection decision code");
}

}  // namespace

int main() {
  try {
    test_valid_outcome_passes_field_rules();
    test_boundary_failure_is_inherited();
    test_whitespace_only_executed_action_rejected();
    test_whitespace_only_rejection_reason_rejected();
    test_collapsed_control_refs_rejected();
    test_failure_attribution_field_stays_rejected();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}