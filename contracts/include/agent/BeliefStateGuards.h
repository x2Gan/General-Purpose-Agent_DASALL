#pragma once

#include <string_view>

#include "agent/BeliefState.h"
#include "boundary/GuardCommon.h"

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// Guard result for BeliefState validation, following the same pattern as
// ObservationDigestGuardResult (WP03-T008) and GoalContractGuardResult
// (WP03-T004).
// ---------------------------------------------------------------------------
struct BeliefStateGuardResult {
  bool ok = false;
  std::string_view reason = "belief state validation failed";
};

// ---------------------------------------------------------------------------
// Layer 1: Required-field presence validation (WP03-T009-B).
//
// Validates that all 6 required fields are present with meaningful values:
//   R1) request_id      — present and non-empty.
//   R2) confirmed_facts  — present (has_value; empty vector is allowed).
//   R3) hypotheses       — present (has_value; empty vector is allowed).
//   R4) assumptions      — present (has_value; empty vector is allowed).
//   R5) evidence_refs    — present (has_value; empty vector is allowed).
//   R6) confidence       — present and in [0.0, 1.0].
//
// Design note:
//   The four vector fields (confirmed_facts, hypotheses, assumptions,
//   evidence_refs) allow empty vectors because a newly initialized
//   belief state may legitimately have no facts, hypotheses, assumptions,
//   or evidence yet.  Whether the belief state is "rich enough" for
//   downstream consumers is judged by the confidence value.
// ---------------------------------------------------------------------------
inline BeliefStateGuardResult
validate_belief_state_required_fields(const BeliefState& state) {
  // R1: request_id must be present and non-empty.
  if (!has_non_empty_value(state.request_id)) {
    return BeliefStateGuardResult{
        .ok = false,
        .reason = "request_id is required and must be non-empty",
    };
  }

  // R2: confirmed_facts must be present (empty vector OK).
  if (!state.confirmed_facts.has_value()) {
    return BeliefStateGuardResult{
        .ok = false,
        .reason = "confirmed_facts is required",
    };
  }

  // R3: hypotheses must be present (empty vector OK).
  if (!state.hypotheses.has_value()) {
    return BeliefStateGuardResult{
        .ok = false,
        .reason = "hypotheses is required",
    };
  }

  // R4: assumptions must be present (empty vector OK).
  if (!state.assumptions.has_value()) {
    return BeliefStateGuardResult{
        .ok = false,
        .reason = "assumptions is required",
    };
  }

  // R5: evidence_refs must be present (empty vector OK).
  if (!state.evidence_refs.has_value()) {
    return BeliefStateGuardResult{
        .ok = false,
        .reason = "evidence_refs is required",
    };
  }

  // R6: confidence must be present and in [0.0, 1.0].
  if (!state.confidence.has_value()) {
    return BeliefStateGuardResult{
        .ok = false,
        .reason = "confidence is required",
    };
  }
  if (*state.confidence < 0.0f || *state.confidence > 1.0f) {
    return BeliefStateGuardResult{
        .ok = false,
        .reason = "confidence must be in [0.0, 1.0]",
    };
  }

  return BeliefStateGuardResult{
      .ok = true,
      .reason = "all required fields present",
  };
}

// ---------------------------------------------------------------------------
// Layer 2: Boundary constraint validation (WP03-T009-B).
//
// Validates semantic boundary rules on top of required fields:
//   All required-field checks (Layer 1).
//   R7) goal_id, if present, must be non-empty.
//   R8) created_at, if present, must be positive.
//
// This layer ensures optional fields comply with their documented constraints
// without introducing new required-ness.  The "boundary" aspect also
// conceptually enforces the prohibition of execution/snapshot/rendering
// fields — structurally guaranteed by the BeliefState struct definition
// which simply does not declare those fields.
// ---------------------------------------------------------------------------
inline BeliefStateGuardResult
validate_belief_state_boundary(const BeliefState& state) {
  // Layer 1: required-field presence.
  auto required_result = validate_belief_state_required_fields(state);
  if (!required_result.ok) {
    return required_result;
  }

  // R7: goal_id, if present, must be non-empty.
  if (state.goal_id.has_value() && state.goal_id->empty()) {
    return BeliefStateGuardResult{
        .ok = false,
        .reason = "goal_id must be non-empty when present",
    };
  }

  // R8: created_at, if present, must be positive (WP02-T010).
  if (state.created_at.has_value() && *state.created_at <= 0) {
    return BeliefStateGuardResult{
        .ok = false,
        .reason = "created_at must be positive when present",
    };
  }

  return BeliefStateGuardResult{
      .ok = true,
      .reason = "belief state boundary validation passed",
  };
}

}  // namespace dasall::contracts
