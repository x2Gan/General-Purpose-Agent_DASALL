// ============================================================================
// RecoveryOutcomeContractTest.cpp
//
// WP04-T011-B: Contract test for RecoveryOutcome.h and RecoveryOutcomeGuards.h.
//
// Validates that RecoveryOutcome remains a runtime-owned execution-result object:
//   - Required result fields are present and meaningful.
//   - Boundary rules keep audit metadata well-formed and mutually exclusive.
//   - ADR-007 failure-attribution fields are still rejected through the reused
//     recovery boundary guard entry point.
//
// Verification command (WP04-T011):
//   cmake --build build-ci --target dasall_contract_tests &&
//   ctest --test-dir build-ci -R RecoveryOutcomeContractTest --output-on-failure
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
using dasall::contracts::validate_recovery_outcome_boundary;
using dasall::contracts::validate_recovery_outcome_contract_field_boundary;
using dasall::contracts::validate_recovery_outcome_required_fields;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

RecoveryOutcome make_valid_outcome() {
  RecoveryOutcome outcome;
  outcome.executed_action = "retry_step";
  outcome.final_runtime_state = "running";
  outcome.updated_retry_count = 2U;
  outcome.checkpoint_ref = "cp-011";
  return outcome;
}

void test_valid_minimal_outcome_passes_required_fields() {
  const auto outcome = make_valid_outcome();
  const auto result = validate_recovery_outcome_required_fields(outcome);

  assert_true(result.ok,
              "valid RecoveryOutcome must pass required-field guard");
}

void test_valid_boundary_outcome_passes_boundary_guard() {
  auto outcome = make_valid_outcome();
  outcome.rejection_reason = "retry budget denied by runtime policy";

  const auto result = validate_recovery_outcome_boundary(outcome);
  assert_true(result.ok,
              "valid RecoveryOutcome metadata must pass boundary guard");
}

void test_allowed_execution_field_stays_allowed() {
  const auto result =
      validate_recovery_outcome_contract_field_boundary("executed_action");

  assert_true(result.allowed,
              "executed_action must remain an allowed RecoveryOutcome field");
  assert_equal(static_cast<int>(RecoveryBoundaryDecision::AllowField),
               static_cast<int>(result.decision),
               "allowed execution field must preserve allow decision code");
}

void test_missing_executed_action_rejected() {
  auto outcome = make_valid_outcome();
  outcome.executed_action = std::nullopt;

  const auto result = validate_recovery_outcome_required_fields(outcome);
  assert_true(!result.ok, "missing executed_action must be rejected");
  assert_equal("executed_action is required and must be non-empty",
               std::string(result.reason),
               "missing executed_action must return canonical reason");
}

void test_missing_final_runtime_state_rejected() {
  auto outcome = make_valid_outcome();
  outcome.final_runtime_state = std::nullopt;

  const auto result = validate_recovery_outcome_required_fields(outcome);
  assert_true(!result.ok, "missing final_runtime_state must be rejected");
  assert_equal("final_runtime_state is required and must be non-empty",
               std::string(result.reason),
               "missing final_runtime_state must return canonical reason");
}

void test_empty_checkpoint_ref_rejected() {
  auto outcome = make_valid_outcome();
  outcome.checkpoint_ref = "";

  const auto result = validate_recovery_outcome_boundary(outcome);
  assert_true(!result.ok, "empty checkpoint_ref must be rejected");
  assert_equal("checkpoint_ref must be non-empty when present",
               std::string(result.reason),
               "empty checkpoint_ref must return canonical reason");
}

void test_dual_audit_reasons_rejected() {
  auto outcome = make_valid_outcome();
  outcome.rejection_reason = "denied by runtime gate";
  outcome.escalation_reason = "manual review requested";

  const auto result = validate_recovery_outcome_boundary(outcome);
  assert_true(!result.ok,
              "rejection_reason and escalation_reason must be mutually exclusive");
  assert_equal("rejection_reason and escalation_reason must not both be present",
               std::string(result.reason),
               "dual audit reasons must return canonical reason");
}

void test_failure_attribution_field_rejected() {
  const auto result =
      validate_recovery_outcome_contract_field_boundary("failure_root_cause");

  assert_true(!result.allowed,
              "failure_root_cause must be rejected for RecoveryOutcome");
  assert_equal(
      static_cast<int>(RecoveryBoundaryDecision::RejectRecoveryAttributionField),
      static_cast<int>(result.decision),
      "failure attribution intrusion must preserve rejection decision code");
  assert_equal("recovery outcome must not contain failure attribution semantics",
               std::string(result.reason),
               "failure attribution intrusion must return canonical reason");
}

}  // namespace

int main() {
  try {
    test_valid_minimal_outcome_passes_required_fields();
    test_valid_boundary_outcome_passes_boundary_guard();
    test_allowed_execution_field_stays_allowed();

    test_missing_executed_action_rejected();
    test_missing_final_runtime_state_rejected();
    test_empty_checkpoint_ref_rejected();
    test_dual_audit_reasons_rejected();
    test_failure_attribution_field_rejected();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}