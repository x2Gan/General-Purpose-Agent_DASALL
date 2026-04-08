// ==========================================================================
// ReflectionDecisionFieldContractTest.cpp
//
// WP04-T008-B: Field-level contract test for ReflectionDecisionGuards.h.
//
// Validates the T008 field-table rules layered on top of the T007 object guard:
//   - Required and boundary checks are inherited by the field-rules guard.
//   - confidence must be finite when present.
//   - relevant_observation_refs must be non-empty, contain no empty strings,
//     and remain a unique observation-reference set.
//   - tags follow the repository-wide vector hygiene rule.
//   - ADR-007 scheduling fields remain rejected through the existing boundary
//     field-name catalog.
// ==========================================================================

#include <cmath>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "checkpoint/ReflectionDecision.h"
#include "checkpoint/ReflectionDecisionGuards.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::RecoveryBoundaryDecision;
using dasall::contracts::ReflectionDecision;
using dasall::contracts::ReflectionDecisionKind;
using dasall::contracts::validate_reflection_decision_contract_field_boundary;
using dasall::contracts::validate_reflection_decision_field_rules;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

ReflectionDecision make_valid_decision() {
  ReflectionDecision decision;
  decision.request_id = "req-field-008";
  decision.decision_kind = ReflectionDecisionKind::Replan;
  decision.rationale = "current evidence indicates the plan assumptions no longer hold";
  return decision;
}

void test_minimal_valid_decision_passes_field_rules() {
  const auto decision = make_valid_decision();
  const auto result = validate_reflection_decision_field_rules(decision);
  assert_true(result.ok,
              "minimal valid ReflectionDecision should pass field rules");
}

void test_full_valid_decision_passes_field_rules() {
  auto decision = make_valid_decision();
  decision.goal_id = "goal-008";
  decision.confidence = 0.65F;
  decision.relevant_observation_refs =
      std::vector<std::string>{"obs-001", "obs-002"};
  decision.hint_ref = "hint:replan:missing-prerequisite";
  decision.created_at = 1710115200000;
  decision.tags = std::vector<std::string>{"reflection", "replan"};

  const auto result = validate_reflection_decision_field_rules(decision);
  assert_true(result.ok,
              "full valid ReflectionDecision should pass field rules");
}

void test_boundary_failure_is_inherited_by_field_rules() {
  auto decision = make_valid_decision();
  decision.hint_ref = "";

  const auto result = validate_reflection_decision_field_rules(decision);
  assert_true(!result.ok,
              "field-rules guard should inherit boundary failure for empty hint_ref");
  assert_equal("hint_ref must be non-empty when present",
               std::string(result.reason),
               "inherited boundary failure should preserve canonical reason");
}

void test_nan_confidence_rejected() {
  auto decision = make_valid_decision();
  decision.confidence = std::nanf("");

  const auto result = validate_reflection_decision_field_rules(decision);
  assert_true(!result.ok,
              "NaN confidence should be rejected by field rules");
  assert_equal("confidence must be a finite value when present",
               std::string(result.reason),
               "NaN confidence should return canonical reason");
}

void test_empty_observation_refs_vector_rejected() {
  auto decision = make_valid_decision();
  decision.relevant_observation_refs = std::vector<std::string>{};

  const auto result = validate_reflection_decision_field_rules(decision);
  assert_true(!result.ok,
              "empty relevant_observation_refs vector should be rejected");
  assert_equal(
      "relevant_observation_refs must contain at least one item when present",
      std::string(result.reason),
      "empty relevant_observation_refs should return canonical reason");
}

void test_empty_observation_ref_rejected() {
  auto decision = make_valid_decision();
  decision.relevant_observation_refs = std::vector<std::string>{"obs-001", ""};

  const auto result = validate_reflection_decision_field_rules(decision);
  assert_true(!result.ok,
              "empty relevant_observation_refs element should be rejected");
  assert_equal(
      "relevant_observation_refs must not contain empty-string elements",
      std::string(result.reason),
      "empty relevant_observation_refs element should return canonical reason");
}

void test_duplicate_observation_refs_rejected() {
  auto decision = make_valid_decision();
  decision.relevant_observation_refs =
      std::vector<std::string>{"obs-001", "obs-002", "obs-001"};

  const auto result = validate_reflection_decision_field_rules(decision);
  assert_true(!result.ok,
              "duplicate relevant_observation_refs should be rejected");
  assert_equal(
      "relevant_observation_refs must not contain duplicate observation identifiers",
      std::string(result.reason),
      "duplicate relevant_observation_refs should return canonical reason");
}

void test_empty_tags_vector_rejected() {
  auto decision = make_valid_decision();
  decision.tags = std::vector<std::string>{};

  const auto result = validate_reflection_decision_field_rules(decision);
  assert_true(!result.ok,
              "empty tags vector should be rejected");
  assert_equal("tags must contain at least one item when present",
               std::string(result.reason),
               "empty tags should return canonical reason");
}

void test_empty_tag_element_rejected() {
  auto decision = make_valid_decision();
  decision.tags = std::vector<std::string>{"reflection", ""};

  const auto result = validate_reflection_decision_field_rules(decision);
  assert_true(!result.ok,
              "empty tag element should be rejected");
  assert_equal("tags must not contain empty-string elements",
               std::string(result.reason),
               "empty tag element should return canonical reason");
}

void test_scheduling_field_name_still_rejected() {
  const auto result =
      validate_reflection_decision_contract_field_boundary("retry_after_ms");

  assert_true(!result.allowed,
              "retry_after_ms must remain rejected for ReflectionDecision");
  assert_equal(
      static_cast<int>(
          RecoveryBoundaryDecision::RejectReflectionSchedulingField),
      static_cast<int>(result.decision),
      "scheduling field should preserve ADR-007 rejection decision code");
}

}  // namespace

int main() {
  try {
    test_minimal_valid_decision_passes_field_rules();
    test_full_valid_decision_passes_field_rules();
    test_boundary_failure_is_inherited_by_field_rules();
    test_nan_confidence_rejected();
    test_empty_observation_refs_vector_rejected();
    test_empty_observation_ref_rejected();
    test_duplicate_observation_refs_rejected();
    test_empty_tags_vector_rejected();
    test_empty_tag_element_rejected();
    test_scheduling_field_name_still_rejected();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}