#pragma once

#include <string_view>

namespace dasall::contracts {

// FieldEvolutionDecision is the executable counterpart of WP02-T002/T003
// classification output and maps directly to checklist item A4.
enum class FieldEvolutionDecision {
  NonBreaking,
  ReviewRequired,
  Breaking,
};

// FieldEvolutionResult keeps both binary gateability and traceable reason text.
// - allowed_for_direct_merge=true means the change can pass without dedicated
//   compatibility review.
// - allowed_for_direct_merge=false means review-required or breaking.
struct FieldEvolutionResult {
  FieldEvolutionDecision decision = FieldEvolutionDecision::ReviewRequired;
  bool allowed_for_direct_merge = false;
  std::string_view reason = "classification pending";
  std::string_view checklist_item = "A4";
};

// TypeEvolutionChange encodes B1/T002 rules for field-type evolution.
// Existing-field in-place type rewrite is breaking; additive parallel fields are
// non-breaking candidates when old semantics remain intact.
struct TypeEvolutionChange {
  bool modifies_existing_field_type = false;
  bool adds_parallel_field = false;
  bool keeps_legacy_field_semantics = true;
  bool changes_consumer_semantics = false;
};

// OptionalityEvolutionChange encodes B2/T003 rules.
// Optional->required for an existing field is breaking by default.
struct OptionalityEvolutionChange {
  bool modifies_existing_field_optionality = false;
  bool optional_to_required = false;
  bool adds_new_optional_field = false;
  bool new_optional_field_changes_behavior_when_absent = false;
};

// CardinalityEvolutionChange encodes B3/T003 rules.
// Multi->single is breaking; single->multi usually needs review unless proven
// harmless for existing consumers.
struct CardinalityEvolutionChange {
  bool expands_single_to_multi = false;
  bool narrows_multi_to_single = false;
  bool consumer_support_for_multi_is_proven = false;
};

inline FieldEvolutionResult classify_type_evolution(const TypeEvolutionChange& change) {
  if (change.modifies_existing_field_type || change.changes_consumer_semantics ||
      !change.keeps_legacy_field_semantics) {
    return FieldEvolutionResult{
        .decision = FieldEvolutionDecision::Breaking,
        .allowed_for_direct_merge = false,
        .reason = "modifying existing field type or semantics is breaking",
        .checklist_item = "B1",
    };
  }

  if (change.adds_parallel_field) {
    return FieldEvolutionResult{
        .decision = FieldEvolutionDecision::NonBreaking,
        .allowed_for_direct_merge = true,
        .reason = "additive parallel type field keeps legacy semantics",
        .checklist_item = "B1",
    };
  }

  return FieldEvolutionResult{
      .decision = FieldEvolutionDecision::ReviewRequired,
      .allowed_for_direct_merge = false,
      .reason = "type evolution impact is unclear and needs review",
      .checklist_item = "A4",
  };
}

inline FieldEvolutionResult classify_optionality_evolution(const OptionalityEvolutionChange& change) {
  if (change.modifies_existing_field_optionality && change.optional_to_required) {
    return FieldEvolutionResult{
        .decision = FieldEvolutionDecision::Breaking,
        .allowed_for_direct_merge = false,
        .reason = "making existing optional field required is breaking",
        .checklist_item = "B2",
    };
  }

  if (change.adds_new_optional_field) {
    if (change.new_optional_field_changes_behavior_when_absent) {
      return FieldEvolutionResult{
          .decision = FieldEvolutionDecision::ReviewRequired,
          .allowed_for_direct_merge = false,
          .reason = "new optional field changes behavior when absent",
          .checklist_item = "B2",
      };
    }

    return FieldEvolutionResult{
        .decision = FieldEvolutionDecision::NonBreaking,
        .allowed_for_direct_merge = true,
        .reason = "new optional field is safely ignorable",
        .checklist_item = "B2",
    };
  }

  return FieldEvolutionResult{
      .decision = FieldEvolutionDecision::ReviewRequired,
      .allowed_for_direct_merge = false,
      .reason = "optionality evolution impact is unclear and needs review",
      .checklist_item = "A4",
  };
}

inline FieldEvolutionResult classify_cardinality_evolution(const CardinalityEvolutionChange& change) {
  if (change.narrows_multi_to_single) {
    return FieldEvolutionResult{
        .decision = FieldEvolutionDecision::Breaking,
        .allowed_for_direct_merge = false,
        .reason = "narrowing multi-value field to single-value is breaking",
        .checklist_item = "B3",
    };
  }

  if (change.expands_single_to_multi) {
    if (change.consumer_support_for_multi_is_proven) {
      return FieldEvolutionResult{
          .decision = FieldEvolutionDecision::NonBreaking,
          .allowed_for_direct_merge = true,
          .reason = "single-to-multi expansion is proven safe for consumers",
          .checklist_item = "B3",
      };
    }

    return FieldEvolutionResult{
        .decision = FieldEvolutionDecision::ReviewRequired,
        .allowed_for_direct_merge = false,
        .reason = "single-to-multi expansion needs compatibility review",
        .checklist_item = "B3",
    };
  }

  return FieldEvolutionResult{
      .decision = FieldEvolutionDecision::ReviewRequired,
      .allowed_for_direct_merge = false,
      .reason = "cardinality evolution impact is unclear and needs review",
      .checklist_item = "A4",
  };
}

}  // namespace dasall::contracts
