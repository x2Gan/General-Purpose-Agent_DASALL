#include <exception>
#include <iostream>
#include <string>

#include "boundary/RecoveryBoundaryGuards.h"
#include "support/TestAssertions.h"

namespace {

void test_reflection_semantic_field_is_allowed() {
  using dasall::contracts::RecoveryBoundaryDecision;
  using dasall::contracts::evaluate_reflection_decision_field_boundary;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto result = evaluate_reflection_decision_field_boundary("decision_kind");

  // Positive case: ReflectionDecision semantic suggestion fields remain valid.
  assert_true(result.allowed,
              "decision_kind should be allowed in ReflectionDecision");
  assert_equal(static_cast<int>(RecoveryBoundaryDecision::AllowField),
               static_cast<int>(result.decision),
               "semantic reflection field should return allow decision");
}

void test_reflection_scheduling_field_is_rejected() {
  using dasall::contracts::RecoveryBoundaryDecision;
  using dasall::contracts::evaluate_reflection_decision_field_boundary;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto result = evaluate_reflection_decision_field_boundary("retry_after_ms");

  // Negative case: ReflectionDecision must not carry runtime scheduling fields.
  assert_true(!result.allowed,
              "retry_after_ms must be rejected in ReflectionDecision");
  assert_equal(static_cast<int>(RecoveryBoundaryDecision::RejectReflectionSchedulingField),
               static_cast<int>(result.decision),
               "scheduling field should return reflection rejection decision");
}

void test_recovery_execution_field_is_allowed() {
  using dasall::contracts::RecoveryBoundaryDecision;
  using dasall::contracts::evaluate_recovery_outcome_field_boundary;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto result = evaluate_recovery_outcome_field_boundary("executed_action");

  // Positive case: RecoveryOutcome execution metadata should be allowed.
  assert_true(result.allowed,
              "executed_action should be allowed in RecoveryOutcome");
  assert_equal(static_cast<int>(RecoveryBoundaryDecision::AllowField),
               static_cast<int>(result.decision),
               "execution field should return allow decision");
}

void test_recovery_failure_attribution_field_is_rejected() {
  using dasall::contracts::RecoveryBoundaryDecision;
  using dasall::contracts::evaluate_recovery_outcome_field_boundary;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto result = evaluate_recovery_outcome_field_boundary("failure_root_cause");

  // Negative case: RecoveryOutcome must not absorb failure attribution semantics.
  assert_true(!result.allowed,
              "failure_root_cause must be rejected in RecoveryOutcome");
  assert_equal(static_cast<int>(RecoveryBoundaryDecision::RejectRecoveryAttributionField),
               static_cast<int>(result.decision),
               "attribution field should return recovery rejection decision");
  assert_equal("recovery outcome must not contain failure attribution semantics",
               std::string(result.reason),
               "attribution rejection should return normalized reason");
}

void test_recovery_semantics_combination_regression_matrix() {
  using dasall::contracts::RecoveryBoundaryDecision;
  using dasall::contracts::evaluate_recovery_outcome_field_boundary;
  using dasall::contracts::evaluate_reflection_decision_field_boundary;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  struct ReflectionCase {
    const char* field_name;
    bool expected_allowed;
    RecoveryBoundaryDecision expected_decision;
    const char* expected_reason;
  };

  struct OutcomeCase {
    const char* field_name;
    bool expected_allowed;
    RecoveryBoundaryDecision expected_decision;
    const char* expected_reason;
  };

  // The positive pair models the ADR-007 split where ReflectionDecision keeps
  // suggestion semantics while RecoveryOutcome keeps execution-result semantics.
  constexpr ReflectionCase kReflectionPositiveCase{
      "decision_kind",
      true,
      RecoveryBoundaryDecision::AllowField,
      "recovery boundary field is allowed by ADR-007",
  };
  constexpr OutcomeCase kOutcomePositiveCase{
      "executed_action",
      true,
      RecoveryBoundaryDecision::AllowField,
      "recovery boundary field is allowed by ADR-007",
  };

  const auto reflection_positive_result =
      evaluate_reflection_decision_field_boundary(kReflectionPositiveCase.field_name);
  const auto outcome_positive_result =
      evaluate_recovery_outcome_field_boundary(kOutcomePositiveCase.field_name);

  assert_true(reflection_positive_result.allowed == kReflectionPositiveCase.expected_allowed,
              "positive reflection combination should be allowed");
  assert_equal(static_cast<int>(kReflectionPositiveCase.expected_decision),
               static_cast<int>(reflection_positive_result.decision),
               "positive reflection combination should keep allow decision");
  assert_equal(std::string(kReflectionPositiveCase.expected_reason),
               std::string(reflection_positive_result.reason),
               "positive reflection combination should keep normalized allow reason");

  assert_true(outcome_positive_result.allowed == kOutcomePositiveCase.expected_allowed,
              "positive outcome combination should be allowed");
  assert_equal(static_cast<int>(kOutcomePositiveCase.expected_decision),
               static_cast<int>(outcome_positive_result.decision),
               "positive outcome combination should keep allow decision");
  assert_equal(std::string(kOutcomePositiveCase.expected_reason),
               std::string(outcome_positive_result.reason),
               "positive outcome combination should keep normalized allow reason");

  // The negative matrix keeps three representative violations required by
  // WP01-B008: two runtime-scheduling intrusions into ReflectionDecision and
  // one failure-attribution intrusion into RecoveryOutcome.
  constexpr ReflectionCase kReflectionNegativeCases[] = {
      {"retry_after_ms",
       false,
       RecoveryBoundaryDecision::RejectReflectionSchedulingField,
       "reflection decision must not contain runtime scheduling fields"},
      {"backoff_strategy",
       false,
       RecoveryBoundaryDecision::RejectReflectionSchedulingField,
       "reflection decision must not contain runtime scheduling fields"},
  };
  constexpr OutcomeCase kOutcomeNegativeCases[] = {
      {"failure_root_cause",
       false,
       RecoveryBoundaryDecision::RejectRecoveryAttributionField,
       "recovery outcome must not contain failure attribution semantics"},
  };

  for (const auto& reflection_case : kReflectionNegativeCases) {
    const auto result =
        evaluate_reflection_decision_field_boundary(reflection_case.field_name);
    assert_true(result.allowed == reflection_case.expected_allowed,
                "negative reflection combination should be rejected");
    assert_equal(static_cast<int>(reflection_case.expected_decision),
                 static_cast<int>(result.decision),
                 "negative reflection combination should map to scheduling rejection");
    assert_equal(std::string(reflection_case.expected_reason),
                 std::string(result.reason),
                 "negative reflection combination should return normalized rejection reason");
  }

  for (const auto& outcome_case : kOutcomeNegativeCases) {
    const auto result = evaluate_recovery_outcome_field_boundary(outcome_case.field_name);
    assert_true(result.allowed == outcome_case.expected_allowed,
                "negative outcome combination should be rejected");
    assert_equal(static_cast<int>(outcome_case.expected_decision),
                 static_cast<int>(result.decision),
                 "negative outcome combination should map to attribution rejection");
    assert_equal(std::string(outcome_case.expected_reason),
                 std::string(result.reason),
                 "negative outcome combination should return normalized rejection reason");
  }
}

}  // namespace

int main() {
  try {
    test_reflection_semantic_field_is_allowed();
    test_reflection_scheduling_field_is_rejected();
    test_recovery_execution_field_is_allowed();
    test_recovery_failure_attribution_field_is_rejected();
    test_recovery_semantics_combination_regression_matrix();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
