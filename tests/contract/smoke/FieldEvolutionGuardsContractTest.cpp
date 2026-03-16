#include <exception>
#include <iostream>
#include <string>

#include "boundary/FieldEvolutionGuards.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_type_additive_change_is_non_breaking() {
  using dasall::contracts::FieldEvolutionDecision;
  using dasall::contracts::TypeEvolutionChange;
  using dasall::contracts::classify_type_evolution;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  // Positive case: additive parallel field keeps the legacy field semantics,
  // so existing consumers remain compatible without forced migration.
  const TypeEvolutionChange change{
      .modifies_existing_field_type = false,
      .adds_parallel_field = true,
      .keeps_legacy_field_semantics = true,
      .changes_consumer_semantics = false,
  };

  const auto result = classify_type_evolution(change);
  assert_equal(static_cast<int>(FieldEvolutionDecision::NonBreaking),
               static_cast<int>(result.decision),
               "additive parallel type field should be classified as non-breaking");
  assert_true(result.allowed_for_direct_merge,
              "non-breaking type evolution should be mergeable without dedicated review");
}

void test_single_to_multi_without_proof_is_review_required() {
  using dasall::contracts::CardinalityEvolutionChange;
  using dasall::contracts::FieldEvolutionDecision;
  using dasall::contracts::classify_cardinality_evolution;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  // Review-required case: single->multi can alter consumer interpretation when
  // compatibility evidence is missing.
  const CardinalityEvolutionChange change{
      .expands_single_to_multi = true,
      .narrows_multi_to_single = false,
      .consumer_support_for_multi_is_proven = false,
  };

  const auto result = classify_cardinality_evolution(change);
  assert_equal(static_cast<int>(FieldEvolutionDecision::ReviewRequired),
               static_cast<int>(result.decision),
               "single-to-multi expansion without proof should require review");
  assert_true(!result.allowed_for_direct_merge,
              "review-required changes must not pass direct-merge gate");
}

void test_optional_to_required_is_breaking() {
  using dasall::contracts::FieldEvolutionDecision;
  using dasall::contracts::OptionalityEvolutionChange;
  using dasall::contracts::classify_optionality_evolution;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  // Negative case: optional->required raises validation threshold for existing
  // payloads and is therefore breaking by T003/B2.
  const OptionalityEvolutionChange change{
      .modifies_existing_field_optionality = true,
      .optional_to_required = true,
      .adds_new_optional_field = false,
      .new_optional_field_changes_behavior_when_absent = false,
  };

  const auto result = classify_optionality_evolution(change);
  assert_equal(static_cast<int>(FieldEvolutionDecision::Breaking),
               static_cast<int>(result.decision),
               "optional-to-required transition should be classified as breaking");
  assert_true(!result.allowed_for_direct_merge,
              "breaking optionality change must fail direct-merge gate");
}

}  // namespace

int main() {
  try {
    test_type_additive_change_is_non_breaking();
    test_single_to_multi_without_proof_is_review_required();
    test_optional_to_required_is_breaking();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
