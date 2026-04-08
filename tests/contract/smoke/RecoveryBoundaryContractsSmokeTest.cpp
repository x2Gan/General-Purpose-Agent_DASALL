// ==========================================================================
// RecoveryBoundaryContractsSmokeTest.cpp
//
// WP04-T006-B: Smoke test for RecoveryBoundaryContracts.h.
//
// Validates that the ADR-007 Recovery aggregate entry point:
//   1. Exposes the three direct-impact objects captured by T006-D.
//   2. Preserves owner-layer separation between cognition and runtime.
//   3. Re-exports the existing Recovery boundary guards for both positive and
//      negative cases without redefining recovery semantics in a second place.
//
// Verification command (WP04-T006):
//   cmake --build build-ci --target dasall_contract_tests
//   ctest --test-dir build-ci -R RecoveryBoundaryContractsSmokeTest --output-on-failure
// ==========================================================================
#include <exception>
#include <iostream>
#include <string>

#include "checkpoint/RecoveryBoundaryContracts.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::RecoveryBoundaryDecision;
using dasall::contracts::RecoveryBoundaryLayer;
using dasall::contracts::RecoveryBoundaryObject;
using dasall::contracts::evaluate_recovery_outcome_field_boundary;
using dasall::contracts::evaluate_reflection_decision_field_boundary;
using dasall::contracts::is_cognition_owned_recovery_object;
using dasall::contracts::is_recovery_boundary_object;
using dasall::contracts::is_runtime_owned_recovery_object;
using dasall::contracts::kRecoveryBoundaryObjectCatalog;
using dasall::contracts::recovery_boundary_object_name;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

void test_catalog_contains_three_direct_impact_objects() {
  assert_equal(static_cast<int>(3),
               static_cast<int>(kRecoveryBoundaryObjectCatalog.size()),
               "recovery boundary aggregate should expose exactly three T006 objects");

  assert_equal("ReflectionDecision",
               std::string(recovery_boundary_object_name(RecoveryBoundaryObject::ReflectionDecision)),
               "catalog should contain ReflectionDecision");
  assert_equal("RecoveryRequest",
               std::string(recovery_boundary_object_name(RecoveryBoundaryObject::RecoveryRequest)),
               "catalog should contain RecoveryRequest");
  assert_equal("RecoveryOutcome",
               std::string(recovery_boundary_object_name(RecoveryBoundaryObject::RecoveryOutcome)),
               "catalog should contain RecoveryOutcome");
}

void test_catalog_preserves_owner_layer_split() {
  assert_true(is_cognition_owned_recovery_object(RecoveryBoundaryObject::ReflectionDecision),
              "ReflectionDecision must remain cognition-owned under ADR-007");
  assert_true(is_runtime_owned_recovery_object(RecoveryBoundaryObject::RecoveryRequest),
              "RecoveryRequest must remain runtime-owned under ADR-007");
  assert_true(is_runtime_owned_recovery_object(RecoveryBoundaryObject::RecoveryOutcome),
              "RecoveryOutcome must remain runtime-owned under ADR-007");

  assert_equal(static_cast<int>(RecoveryBoundaryLayer::Cognition),
               static_cast<int>(kRecoveryBoundaryObjectCatalog[0].owner_layer),
               "first catalog entry should preserve cognition ownership");
  assert_equal(static_cast<int>(RecoveryBoundaryLayer::Runtime),
               static_cast<int>(kRecoveryBoundaryObjectCatalog[1].owner_layer),
               "second catalog entry should preserve runtime ownership");
  assert_equal(static_cast<int>(RecoveryBoundaryLayer::Runtime),
               static_cast<int>(kRecoveryBoundaryObjectCatalog[2].owner_layer),
               "third catalog entry should preserve runtime ownership");
}

void test_name_lookup_accepts_only_recovery_wave_objects() {
  assert_true(is_recovery_boundary_object("ReflectionDecision"),
              "ReflectionDecision should be recognized by the aggregate header");
  assert_true(is_recovery_boundary_object("RecoveryRequest"),
              "RecoveryRequest should be recognized by the aggregate header");
  assert_true(is_recovery_boundary_object("RecoveryOutcome"),
              "RecoveryOutcome should be recognized by the aggregate header");

  assert_true(!is_recovery_boundary_object("Checkpoint"),
              "Checkpoint is an upstream anchor and must not be duplicated into T006 scope");
  assert_true(!is_recovery_boundary_object("PromptComposeResult"),
              "non-recovery objects must not leak into Recovery boundary aggregate");
}

void test_reflection_guard_is_visible_through_aggregate_include() {
  const auto allowed_result = evaluate_reflection_decision_field_boundary("decision_kind");
  assert_true(allowed_result.allowed,
              "decision_kind should remain allowed through RecoveryBoundaryContracts include");
  assert_equal(static_cast<int>(RecoveryBoundaryDecision::AllowField),
               static_cast<int>(allowed_result.decision),
               "allowed reflection field should keep allow decision");

  const auto rejected_result = evaluate_reflection_decision_field_boundary("retry_after_ms");
  assert_true(!rejected_result.allowed,
              "retry_after_ms must remain rejected through RecoveryBoundaryContracts include");
  assert_equal(static_cast<int>(RecoveryBoundaryDecision::RejectReflectionSchedulingField),
               static_cast<int>(rejected_result.decision),
               "reflection scheduling field should keep scheduling rejection decision");
}

void test_outcome_guard_is_visible_through_aggregate_include() {
  const auto allowed_result = evaluate_recovery_outcome_field_boundary("executed_action");
  assert_true(allowed_result.allowed,
              "executed_action should remain allowed through RecoveryBoundaryContracts include");
  assert_equal(static_cast<int>(RecoveryBoundaryDecision::AllowField),
               static_cast<int>(allowed_result.decision),
               "allowed outcome field should keep allow decision");

  const auto rejected_result = evaluate_recovery_outcome_field_boundary("failure_root_cause");
  assert_true(!rejected_result.allowed,
              "failure_root_cause must remain rejected through RecoveryBoundaryContracts include");
  assert_equal(static_cast<int>(RecoveryBoundaryDecision::RejectRecoveryAttributionField),
               static_cast<int>(rejected_result.decision),
               "outcome attribution field should keep attribution rejection decision");
}

}  // namespace

int main() {
  try {
    test_catalog_contains_three_direct_impact_objects();
    test_catalog_preserves_owner_layer_split();
    test_name_lookup_accepts_only_recovery_wave_objects();
    test_reflection_guard_is_visible_through_aggregate_include();
    test_outcome_guard_is_visible_through_aggregate_include();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}