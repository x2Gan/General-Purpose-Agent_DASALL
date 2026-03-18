#pragma once

#include <cctype>
#include <string_view>

#include "boundary/RecoveryBoundaryGuards.h"
#include "checkpoint/RecoveryOutcome.h"

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// Guard result for RecoveryOutcome validation.
//
// Validation now has three layers:
//   1. Required result fields are present.
//   2. Boundary rules preserve ADR-007 result semantics and keep audit reasons
//      from collapsing into failure-attribution payloads.
//   3. Field rules reject whitespace-only strings and collapsed control refs.
// ---------------------------------------------------------------------------
struct RecoveryOutcomeGuardResult {
  bool ok = false;
  std::string_view reason = "recovery outcome validation failed";
};

inline bool recovery_outcome_string_has_non_whitespace_content(
    std::string_view value) {
  for (const unsigned char ch : value) {
    if (!std::isspace(ch)) {
      return true;
    }
  }
  return false;
}

inline bool recovery_outcome_optional_string_has_non_whitespace_content(
    const std::optional<std::string>& value) {
  return !value.has_value() ||
         recovery_outcome_string_has_non_whitespace_content(*value);
}

// ---------------------------------------------------------------------------
// Layer 1: Required-field presence validation.
//
// Required fields:
//   - executed_action
//   - final_runtime_state
// ---------------------------------------------------------------------------
inline RecoveryOutcomeGuardResult validate_recovery_outcome_required_fields(
    const RecoveryOutcome& outcome) {
  if (!outcome.executed_action.has_value() || outcome.executed_action->empty()) {
    return RecoveryOutcomeGuardResult{
        .ok = false,
        .reason = "executed_action is required and must be non-empty",
    };
  }

  if (!outcome.final_runtime_state.has_value() ||
      outcome.final_runtime_state->empty()) {
    return RecoveryOutcomeGuardResult{
        .ok = false,
        .reason = "final_runtime_state is required and must be non-empty",
    };
  }

  return RecoveryOutcomeGuardResult{
      .ok = true,
      .reason = "all required recovery-outcome fields present",
  };
}

inline bool recovery_outcome_optional_string_is_valid(
    const std::optional<std::string>& value) {
  return !value.has_value() || !value->empty();
}

// ---------------------------------------------------------------------------
// Layer 2: Boundary validation.
//
// T011 keeps these checks at the object boundary level only:
//   1. Optional audit/reference strings must be meaningful when present.
//   2. rejection_reason and escalation_reason are mutually exclusive so the
//      result object does not report two terminal audit outcomes at once.
// ---------------------------------------------------------------------------
inline RecoveryOutcomeGuardResult validate_recovery_outcome_boundary(
    const RecoveryOutcome& outcome) {
  const auto required_result =
      validate_recovery_outcome_required_fields(outcome);
  if (!required_result.ok) {
    return required_result;
  }

  if (!recovery_outcome_optional_string_is_valid(outcome.checkpoint_ref)) {
    return RecoveryOutcomeGuardResult{
        .ok = false,
        .reason = "checkpoint_ref must be non-empty when present",
    };
  }

  if (!recovery_outcome_optional_string_is_valid(
          outcome.compensation_result_ref)) {
    return RecoveryOutcomeGuardResult{
        .ok = false,
        .reason = "compensation_result_ref must be non-empty when present",
    };
  }

  if (!recovery_outcome_optional_string_is_valid(outcome.rejection_reason)) {
    return RecoveryOutcomeGuardResult{
        .ok = false,
        .reason = "rejection_reason must be non-empty when present",
    };
  }

  if (!recovery_outcome_optional_string_is_valid(outcome.escalation_reason)) {
    return RecoveryOutcomeGuardResult{
        .ok = false,
        .reason = "escalation_reason must be non-empty when present",
    };
  }

  if (outcome.rejection_reason.has_value() &&
      outcome.escalation_reason.has_value()) {
    return RecoveryOutcomeGuardResult{
        .ok = false,
        .reason =
            "rejection_reason and escalation_reason must not both be present",
    };
  }

  return RecoveryOutcomeGuardResult{
      .ok = true,
      .reason = "recovery outcome boundary validation passed",
  };
}

// ---------------------------------------------------------------------------
// Field-name boundary wrapper.
//
// RecoveryOutcome value validation stays in this file, while the frozen
// ADR-007 failure-attribution forbidden-field catalog remains delegated to the
// shared RecoveryBoundaryGuards introduced before T011.
// ---------------------------------------------------------------------------
inline RecoveryBoundaryResult validate_recovery_outcome_contract_field_boundary(
    std::string_view field_name) {
  return evaluate_recovery_outcome_field_boundary(field_name);
}

// ---------------------------------------------------------------------------
// Layer 3: Field-level validation.
//
// T012 adds field-table hygiene on top of the T011 required/boundary guards:
//   1. String fields must contain non-whitespace content when present.
//   2. checkpoint_ref and compensation_result_ref must not collapse to the
//      same identifier when both are present.
// ---------------------------------------------------------------------------
inline RecoveryOutcomeGuardResult validate_recovery_outcome_field_rules(
    const RecoveryOutcome& outcome) {
  const auto boundary_result = validate_recovery_outcome_boundary(outcome);
  if (!boundary_result.ok) {
    return boundary_result;
  }

  if (!recovery_outcome_string_has_non_whitespace_content(
          *outcome.executed_action)) {
    return RecoveryOutcomeGuardResult{
        .ok = false,
        .reason =
            "executed_action must contain at least one non-whitespace character",
    };
  }

  if (!recovery_outcome_string_has_non_whitespace_content(
          *outcome.final_runtime_state)) {
    return RecoveryOutcomeGuardResult{
        .ok = false,
        .reason =
            "final_runtime_state must contain at least one non-whitespace character",
    };
  }

  if (!recovery_outcome_optional_string_has_non_whitespace_content(
          outcome.checkpoint_ref)) {
    return RecoveryOutcomeGuardResult{
        .ok = false,
        .reason =
            "checkpoint_ref must contain at least one non-whitespace character when present",
    };
  }

  if (!recovery_outcome_optional_string_has_non_whitespace_content(
          outcome.compensation_result_ref)) {
    return RecoveryOutcomeGuardResult{
        .ok = false,
        .reason =
            "compensation_result_ref must contain at least one non-whitespace character when present",
    };
  }

  if (!recovery_outcome_optional_string_has_non_whitespace_content(
          outcome.rejection_reason)) {
    return RecoveryOutcomeGuardResult{
        .ok = false,
        .reason =
            "rejection_reason must contain at least one non-whitespace character when present",
    };
  }

  if (!recovery_outcome_optional_string_has_non_whitespace_content(
          outcome.escalation_reason)) {
    return RecoveryOutcomeGuardResult{
        .ok = false,
        .reason =
            "escalation_reason must contain at least one non-whitespace character when present",
    };
  }

  if (outcome.checkpoint_ref.has_value() &&
      outcome.compensation_result_ref.has_value() &&
      *outcome.checkpoint_ref == *outcome.compensation_result_ref) {
    return RecoveryOutcomeGuardResult{
        .ok = false,
        .reason =
            "checkpoint_ref and compensation_result_ref must not use the same identifier",
    };
  }

  return RecoveryOutcomeGuardResult{
      .ok = true,
      .reason = "recovery outcome field rules validation passed",
  };
}

}  // namespace dasall::contracts