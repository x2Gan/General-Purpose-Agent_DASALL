#pragma once

#include <string_view>

#include "boundary/RecoveryBoundaryGuards.h"
#include "checkpoint/RecoveryOutcome.h"

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// Guard result for RecoveryOutcome validation.
//
// T011 validates two layers only:
//   1. Required result fields are present.
//   2. Boundary rules preserve ADR-007 result semantics and keep audit reasons
//      from collapsing into failure-attribution payloads.
// ---------------------------------------------------------------------------
struct RecoveryOutcomeGuardResult {
  bool ok = false;
  std::string_view reason = "recovery outcome validation failed";
};

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

}  // namespace dasall::contracts