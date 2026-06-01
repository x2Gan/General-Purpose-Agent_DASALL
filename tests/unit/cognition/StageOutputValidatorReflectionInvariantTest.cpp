#include <exception>
#include <iostream>
#include <string>

#include "validation/StageOutputValidator.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::validation::StageOutputValidator;
using dasall::cognition::validation::ValidationIssueCode;
using dasall::contracts::ReflectionDecision;
using dasall::contracts::ReflectionDecisionKind;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] ReflectionDecision make_valid_reflection_decision() {
  ReflectionDecision decision;
  decision.request_id = "req-reflection-structured";
  decision.decision_kind = ReflectionDecisionKind::RetryStep;
  decision.rationale = "bridge-authored reflection decision keeps the current plan node";
  decision.goal_id = "goal-reflection-structured";
  decision.confidence = 0.83F;
  decision.relevant_observation_refs = std::vector<std::string>{"obs-reflection-001"};
  decision.hint_ref = "hint:reflection:retry_step";
  decision.created_at = 1712746800000LL;
  decision.tags = std::vector<std::string>{"cognition", "reflection"};
  return decision;
}

void test_validate_reflection_decision_invariants_accepts_guarded_payload() {
  StageOutputValidator validator;

  const auto result = validator.validate_reflection_decision_invariants(
      make_valid_reflection_decision());

  assert_true(result.ok, "guard-compliant reflection decisions should pass invariants");
  assert_true(result.issue_set.empty(),
              "guard-compliant reflection decisions should not emit validation issues");
}

void test_validate_reflection_decision_invariants_rejects_missing_request_id() {
  StageOutputValidator validator;
  auto decision = make_valid_reflection_decision();
  decision.request_id = std::nullopt;

  const auto result = validator.validate_reflection_decision_invariants(decision);

  assert_true(!result.ok, "missing request_id must fail reflection invariants");
  assert_equal(1, static_cast<int>(result.issue_set.issues.size()),
               "missing request_id should surface one invariant issue");
  assert_true(result.issue_set.issues.front().code ==
                  ValidationIssueCode::ReflectionDecisionInvariant,
              "reflection invariant failures must use the reflection-specific issue code");
  assert_equal(std::string("request_id"),
               result.issue_set.issues.front().field_path,
               "missing request_id should surface the canonical field path");
}

void test_validate_reflection_decision_invariants_rejects_duplicate_observation_refs() {
  StageOutputValidator validator;
  auto decision = make_valid_reflection_decision();
  decision.relevant_observation_refs =
      std::vector<std::string>{"obs-reflection-001", "obs-reflection-001"};

  const auto result = validator.validate_reflection_decision_invariants(decision);

  assert_true(!result.ok, "duplicate observation refs must fail reflection invariants");
  assert_equal(1, static_cast<int>(result.issue_set.issues.size()),
               "duplicate observation refs should surface one invariant issue");
  assert_equal(std::string("relevant_observation_refs"),
               result.issue_set.issues.front().field_path,
               "duplicate observation refs should surface the evidence field path");
}

}  // namespace

int main() {
  try {
    test_validate_reflection_decision_invariants_accepts_guarded_payload();
    test_validate_reflection_decision_invariants_rejects_missing_request_id();
    test_validate_reflection_decision_invariants_rejects_duplicate_observation_refs();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}