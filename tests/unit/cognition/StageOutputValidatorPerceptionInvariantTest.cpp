#include <exception>
#include <iostream>
#include <string>

#include "perception/PerceptionResult.h"
#include "support/TestAssertions.h"
#include "validation/StageOutputValidator.h"

namespace {

using dasall::cognition::perception::PerceptionResult;
using dasall::cognition::validation::StageOutputValidator;
using dasall::cognition::validation::ValidationIssueCode;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] PerceptionResult make_valid_perception_result() {
  PerceptionResult result;
  result.intent_summary = "plan the next governed step";
  result.task_type = "plan";
  result.entities = {
      dasall::cognition::perception::EntityCandidate{
          .name = "goal",
          .value = "plan the next governed step",
          .confidence = 0.95F,
          .evidence_refs = {"goal_contract.goal_description"},
      },
      dasall::cognition::perception::EntityCandidate{
          .name = "user_turn",
          .value = "please plan the next governed step",
          .confidence = 0.90F,
          .evidence_refs = {"context_packet.user_turn"},
      },
  };
  result.constraints_digest.hard_constraints = {"do not cross runtime ownership"};
  result.constraints_digest.soft_constraints = {"prefer the bounded plan path"};
  result.confidence = 0.84F;
  result.requires_clarification = false;
  result.diagnostics = {"perception.structured_projection"};
  return result;
}

void test_validate_perception_invariants_accepts_consistent_result() {
  StageOutputValidator validator;

  const auto result = validator.validate_perception_invariants(
      make_valid_perception_result());

  assert_true(result.ok, "consistent perception results should pass invariants");
  assert_true(result.issue_set.empty(),
              "consistent perception results should not emit validation issues");
}

void test_validate_perception_invariants_rejects_clarification_without_question() {
  StageOutputValidator validator;
  auto result = make_valid_perception_result();
  result.requires_clarification = true;

  const auto validation = validator.validate_perception_invariants(result);

  assert_true(!validation.ok,
              "perception results that require clarification must fail without a question");
  assert_equal(1, static_cast<int>(validation.issue_set.issues.size()),
               "missing clarification question should surface one invariant issue");
  assert_true(validation.issue_set.issues.front().code == ValidationIssueCode::PerceptionInvariant,
              "perception invariant failures must use the perception-specific issue code");
  assert_equal(std::string("clarification_questions"),
               validation.issue_set.issues.front().field_path,
               "missing clarification questions should surface the canonical field path");
}

void test_validate_perception_invariants_rejects_entity_with_empty_name() {
  StageOutputValidator validator;
  auto result = make_valid_perception_result();
  result.entities.front().name.clear();

  const auto validation = validator.validate_perception_invariants(result);

  assert_true(!validation.ok, "perception entities with an empty name must fail invariants");
  assert_equal(std::string("entities.name"),
               validation.issue_set.issues.front().field_path,
               "entity shape violations should surface the entities.name field path");
}

void test_validate_perception_invariants_rejects_questions_when_flag_is_false() {
  StageOutputValidator validator;
  auto result = make_valid_perception_result();
  result.clarification_questions = {
      dasall::cognition::perception::ClarificationCandidate{
          .question = "Which target should cognition confirm before planning?",
          .evidence_refs = {"context_packet.user_turn"},
          .priority = 0.71F,
      },
  };

  const auto validation = validator.validate_perception_invariants(result);

  assert_true(!validation.ok,
              "clarification questions must not be present when requires_clarification is false");
  assert_equal(std::string("requires_clarification"),
               validation.issue_set.issues.front().field_path,
               "flag/question mismatches should surface the requires_clarification field path");
}

}  // namespace

int main() {
  try {
    test_validate_perception_invariants_accepts_consistent_result();
    test_validate_perception_invariants_rejects_clarification_without_question();
    test_validate_perception_invariants_rejects_entity_with_empty_name();
    test_validate_perception_invariants_rejects_questions_when_flag_is_false();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}