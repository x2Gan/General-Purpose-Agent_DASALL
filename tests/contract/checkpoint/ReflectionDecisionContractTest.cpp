// ============================================================================
// ReflectionDecisionContractTest.cpp
//
// WP04-T007-B: Contract test for ReflectionDecision.h and
// ReflectionDecisionGuards.h.
//
// Validates that ReflectionDecision remains a suggestion-only cognition object:
//   - Required object fields are present and meaningful.
//   - Boundary rules reject invalid enum values and invalid metadata ranges.
//   - ADR-007 scheduling fields are still rejected through the reused recovery
//     boundary guard entry point.
//
// Verification command (WP04-T007):
//   cmake --build build-ci --target dasall_contract_tests &&
//   ctest --test-dir build-ci -R ReflectionDecisionContractTest --output-on-failure
// ============================================================================

#include <exception>
#include <iostream>
#include <string>

#include "checkpoint/ReflectionDecision.h"
#include "checkpoint/ReflectionDecisionGuards.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

using dasall::contracts::ReflectionDecision;
using dasall::contracts::ReflectionDecisionKind;
using dasall::contracts::RecoveryBoundaryDecision;
using dasall::contracts::validate_reflection_decision_boundary;
using dasall::contracts::validate_reflection_decision_contract_field_boundary;
using dasall::contracts::validate_reflection_decision_required_fields;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

ReflectionDecision make_valid_decision() {
  ReflectionDecision decision;
  decision.request_id = "req-007";
  decision.decision_kind = ReflectionDecisionKind::RetryStep;
  decision.rationale = "tool call failed with transient network evidence";
  return decision;
}

void test_valid_minimal_decision_passes_required_fields() {
  const auto decision = make_valid_decision();
  const auto result = validate_reflection_decision_required_fields(decision);

  assert_true(result.ok,
              "minimal valid ReflectionDecision must pass required-field guard");
}

void test_valid_boundary_decision_passes_boundary_guard() {
  auto decision = make_valid_decision();
  decision.confidence = 0.75F;
  decision.hint_ref = "hint:replan:network-retry";
  decision.created_at = 1710000700000;

  const auto result = validate_reflection_decision_boundary(decision);
  assert_true(result.ok,
              "valid ReflectionDecision metadata must pass boundary guard");
}

void test_allowed_field_name_stays_allowed() {
  const auto result =
      validate_reflection_decision_contract_field_boundary("decision_kind");

  assert_true(result.allowed,
              "decision_kind must remain an allowed ReflectionDecision field");
  assert_equal(static_cast<int>(RecoveryBoundaryDecision::AllowField),
               static_cast<int>(result.decision),
               "allowed field must preserve allow decision code");
}

void test_missing_request_id_rejected() {
  auto decision = make_valid_decision();
  decision.request_id = std::nullopt;

  const auto result = validate_reflection_decision_required_fields(decision);
  assert_true(!result.ok, "missing request_id must be rejected");
  assert_equal("request_id is required and must be non-empty",
               std::string(result.reason),
               "missing request_id must return canonical rejection reason");
}

void test_missing_rationale_rejected() {
  auto decision = make_valid_decision();
  decision.rationale = std::nullopt;

  const auto result = validate_reflection_decision_required_fields(decision);
  assert_true(!result.ok, "missing rationale must be rejected");
  assert_equal("rationale is required and must be non-empty",
               std::string(result.reason),
               "missing rationale must return canonical rejection reason");
}

void test_out_of_range_decision_kind_rejected() {
  auto decision = make_valid_decision();
  decision.decision_kind = static_cast<ReflectionDecisionKind>(99);

  const auto result = validate_reflection_decision_boundary(decision);
  assert_true(!result.ok, "out-of-range decision_kind must be rejected");
  assert_equal(
      "decision_kind value is outside the known ReflectionDecisionKind range",
      std::string(result.reason),
      "out-of-range decision_kind must return canonical rejection reason");
}

void test_confidence_out_of_range_rejected() {
  auto decision = make_valid_decision();
  decision.confidence = 1.25F;

  const auto result = validate_reflection_decision_boundary(decision);
  assert_true(!result.ok, "confidence above 1.0 must be rejected");
  assert_equal("confidence must be within [0.0, 1.0] when present",
               std::string(result.reason),
               "invalid confidence must return canonical rejection reason");
}

void test_scheduling_field_name_rejected() {
  const auto result =
      validate_reflection_decision_contract_field_boundary("retry_after_ms");

  assert_true(!result.allowed,
              "retry_after_ms must be rejected for ReflectionDecision");
  assert_equal(
      static_cast<int>(
          RecoveryBoundaryDecision::RejectReflectionSchedulingField),
      static_cast<int>(result.decision),
      "scheduling field must preserve scheduling rejection decision code");
}

}  // namespace

int main() {
  try {
    test_valid_minimal_decision_passes_required_fields();
    test_valid_boundary_decision_passes_boundary_guard();
    test_allowed_field_name_stays_allowed();

    test_missing_request_id_rejected();
    test_missing_rationale_rejected();
    test_out_of_range_decision_kind_rejected();
    test_confidence_out_of_range_rejected();
    test_scheduling_field_name_rejected();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}