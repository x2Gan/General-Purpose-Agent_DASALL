#pragma once

#include <cmath>
#include <string_view>

#include "boundary/RecoveryBoundaryGuards.h"
#include "checkpoint/ReflectionDecision.h"

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// Guard result for ReflectionDecision validation.
//
// T007 validates two layers only:
//   1. Required fields.
//   2. Boundary rules.
// Optional-field hygiene is intentionally deferred to T008 so this task stays
// within the responsibilities/object-guard scope defined by the work package.
// ---------------------------------------------------------------------------
struct ReflectionDecisionGuardResult {
  bool ok = false;
  std::string_view reason = "reflection decision validation failed";
};

// ---------------------------------------------------------------------------
// Layer 1: Required-field presence validation.
//
// Required fields:
//   - request_id
//   - decision_kind
//   - rationale
// ---------------------------------------------------------------------------
inline ReflectionDecisionGuardResult validate_reflection_decision_required_fields(
    const ReflectionDecision& decision) {
  if (!decision.request_id.has_value() || decision.request_id->empty()) {
    return ReflectionDecisionGuardResult{
        .ok = false,
        .reason = "request_id is required and must be non-empty",
    };
  }

  if (!decision.decision_kind.has_value() ||
      *decision.decision_kind == ReflectionDecisionKind::Unspecified) {
    return ReflectionDecisionGuardResult{
        .ok = false,
        .reason = "decision_kind is required and must not be Unspecified",
    };
  }

  if (!decision.rationale.has_value() || decision.rationale->empty()) {
    return ReflectionDecisionGuardResult{
        .ok = false,
        .reason = "rationale is required and must be non-empty",
    };
  }

  return ReflectionDecisionGuardResult{
      .ok = true,
      .reason = "all required fields present",
  };
}

// ---------------------------------------------------------------------------
// Layer 2: Boundary validation.
//
// T007 keeps the boundary checks focused on object-level invariants that are
// necessary to preserve ADR-007 semantics without pre-implementing the T008
// field-table rules.
// ---------------------------------------------------------------------------
inline ReflectionDecisionGuardResult validate_reflection_decision_boundary(
    const ReflectionDecision& decision) {
  const auto required_result =
      validate_reflection_decision_required_fields(decision);
  if (!required_result.ok) {
    return required_result;
  }

  const int raw_kind = static_cast<int>(*decision.decision_kind);
  if (raw_kind < static_cast<int>(ReflectionDecisionKind::Continue) ||
      raw_kind > static_cast<int>(ReflectionDecisionKind::AbortSafe)) {
    return ReflectionDecisionGuardResult{
        .ok = false,
        .reason = "decision_kind value is outside the known ReflectionDecisionKind range",
    };
  }

  if (decision.goal_id.has_value() && decision.goal_id->empty()) {
    return ReflectionDecisionGuardResult{
        .ok = false,
        .reason = "goal_id must be non-empty when present",
    };
  }

  if (decision.confidence.has_value() &&
      (*decision.confidence < 0.0F || *decision.confidence > 1.0F)) {
    return ReflectionDecisionGuardResult{
        .ok = false,
        .reason = "confidence must be within [0.0, 1.0] when present",
    };
  }

  if (decision.hint_ref.has_value() && decision.hint_ref->empty()) {
    return ReflectionDecisionGuardResult{
        .ok = false,
        .reason = "hint_ref must be non-empty when present",
    };
  }

  if (decision.created_at.has_value() && *decision.created_at <= 0) {
    return ReflectionDecisionGuardResult{
        .ok = false,
        .reason = "created_at must be a positive timestamp when present",
    };
  }

  return ReflectionDecisionGuardResult{
      .ok = true,
      .reason = "reflection decision boundary validation passed",
  };
}

// ---------------------------------------------------------------------------
// Layer 3: Field-level validation.
//
// T008 adds field-table hygiene on top of the T007 required/boundary checks:
//   1. All required + boundary checks are inherited.
//   2. confidence, if present, must be a finite numeric value.
//   3. relevant_observation_refs, if present, must be a non-empty vector
//      containing non-empty and unique observation identifiers.
//   4. tags, if present, must be a non-empty vector containing no empty
//      strings, following the repository-wide tags pattern.
//
// This remains intentionally narrower than runtime policy validation: T008 does
// not introduce decision_kind-driven scheduling or execution constraints.
// ---------------------------------------------------------------------------
inline ReflectionDecisionGuardResult validate_reflection_decision_field_rules(
    const ReflectionDecision& decision) {
  const auto boundary_result =
      validate_reflection_decision_boundary(decision);
  if (!boundary_result.ok) {
    return boundary_result;
  }

  if (decision.confidence.has_value() &&
      !std::isfinite(*decision.confidence)) {
    return ReflectionDecisionGuardResult{
        .ok = false,
        .reason = "confidence must be a finite value when present",
    };
  }

  if (decision.relevant_observation_refs.has_value()) {
    if (decision.relevant_observation_refs->empty()) {
      return ReflectionDecisionGuardResult{
          .ok = false,
          .reason = "relevant_observation_refs must contain at least one item when present",
      };
    }

    for (const auto& observation_ref : *decision.relevant_observation_refs) {
      if (observation_ref.empty()) {
        return ReflectionDecisionGuardResult{
            .ok = false,
            .reason = "relevant_observation_refs must not contain empty-string elements",
        };
      }
    }

    for (std::size_t index = 0;
         index < decision.relevant_observation_refs->size();
         ++index) {
      for (std::size_t probe = index + 1;
           probe < decision.relevant_observation_refs->size();
           ++probe) {
        if ((*decision.relevant_observation_refs)[index] ==
            (*decision.relevant_observation_refs)[probe]) {
          return ReflectionDecisionGuardResult{
              .ok = false,
              .reason = "relevant_observation_refs must not contain duplicate observation identifiers",
          };
        }
      }
    }
  }

  if (decision.tags.has_value()) {
    if (decision.tags->empty()) {
      return ReflectionDecisionGuardResult{
          .ok = false,
          .reason = "tags must contain at least one item when present",
      };
    }

    for (const auto& tag : *decision.tags) {
      if (tag.empty()) {
        return ReflectionDecisionGuardResult{
            .ok = false,
            .reason = "tags must not contain empty-string elements",
        };
      }
    }
  }

  return ReflectionDecisionGuardResult{
      .ok = true,
      .reason = "reflection decision field rules validation passed",
  };
}

// ---------------------------------------------------------------------------
// Field-name boundary wrapper.
//
// ReflectionDecision object validation checks concrete field values. Field-name
// boundary validation remains delegated to the frozen ADR-007 Recovery guards
// introduced in T006 so T007 does not duplicate the forbidden-field catalog.
// ---------------------------------------------------------------------------
inline RecoveryBoundaryResult validate_reflection_decision_contract_field_boundary(
    std::string_view field_name) {
  return evaluate_reflection_decision_field_boundary(field_name);
}

}  // namespace dasall::contracts