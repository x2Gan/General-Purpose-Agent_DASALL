#pragma once

#include <string_view>

#include "boundary/GuardCommon.h"
#include "observation/Observation.h"

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// Guard result for Observation validation, following the same pattern as
// GoalContractGuardResult (WP03-T004) and AgentRequestGuardResult (WP03-T002).
// ---------------------------------------------------------------------------
struct ObservationGuardResult {
  bool ok = false;
  std::string_view reason = "observation validation failed";
};

// ---------------------------------------------------------------------------
// Layer 1: Required-field presence validation (WP03-T006-B).
//
// Validates that all 5 required fields are present with meaningful values:
//   1) observation_id — present and non-empty.
//   2) source — present and not Unspecified.
//   3) success — present (has_value).
//   4) payload — present and non-empty.
//   5) created_at — present and positive.
// ---------------------------------------------------------------------------
inline ObservationGuardResult validate_observation_required_fields(
    const Observation& obs) {
  if (!has_non_empty_value(obs.observation_id)) {
    return ObservationGuardResult{
        .ok = false,
        .reason = "observation_id is required and must be non-empty",
    };
  }

  if (!obs.source.has_value() ||
      *obs.source == ObservationSource::Unspecified) {
    return ObservationGuardResult{
        .ok = false,
        .reason = "source is required and must not be Unspecified",
    };
  }

  if (!obs.success.has_value()) {
    return ObservationGuardResult{
        .ok = false,
        .reason = "success is required",
    };
  }

  if (!has_non_empty_value(obs.payload)) {
    return ObservationGuardResult{
        .ok = false,
        .reason = "payload is required and must be non-empty",
    };
  }

  if (!obs.created_at.has_value() || *obs.created_at <= 0) {
    return ObservationGuardResult{
        .ok = false,
        .reason = "created_at is required and must be a positive timestamp",
    };
  }

  return ObservationGuardResult{
      .ok = true,
      .reason = "all required fields present",
  };
}

// ---------------------------------------------------------------------------
// Layer 2: Boundary constraint validation (WP03-T006-B).
//
// Validates semantic boundary rules on top of required fields:
//   1) All required field checks (Layer 1).
//   2) source must be within the known ObservationSource enum range.
//   3) duration_ms, if present, must be positive.
//   4) success=false implies error should be present (warning-level;
//      we enforce this as a hard boundary).
//   5) success=true implies error should be absent.
// ---------------------------------------------------------------------------
inline ObservationGuardResult validate_observation_boundary(
    const Observation& obs) {
  // Layer 1: required field presence.
  auto required_result = validate_observation_required_fields(obs);
  if (!required_result.ok) {
    return required_result;
  }

  // Boundary: ObservationSource enum range (WP02-T012 unknown value guard).
  const int raw_source = static_cast<int>(*obs.source);
  if (raw_source < static_cast<int>(ObservationSource::Unspecified) ||
      raw_source > static_cast<int>(ObservationSource::HumanFeedback)) {
    return ObservationGuardResult{
        .ok = false,
        .reason = "source value is outside the known ObservationSource range",
    };
  }

  // Boundary: duration_ms, if present, must be positive.
  if (obs.duration_ms.has_value() && *obs.duration_ms <= 0) {
    return ObservationGuardResult{
        .ok = false,
        .reason = "duration_ms must be positive when present",
    };
  }

  // Boundary: success/error consistency.
  // When success=false, error should be present to provide structured
  // failure information (architecture 3.8.2: Observation defines ErrorInfo).
  if (obs.success.has_value() && !*obs.success && !obs.error.has_value()) {
    return ObservationGuardResult{
        .ok = false,
        .reason = "error must be present when success is false",
    };
  }

  // When success=true, error should not be present to avoid semantic
  // ambiguity between success status and error info.
  if (obs.success.has_value() && *obs.success && obs.error.has_value()) {
    return ObservationGuardResult{
        .ok = false,
        .reason = "error must not be present when success is true",
    };
  }

  return ObservationGuardResult{
      .ok = true,
      .reason = "observation boundary validation passed",
  };
}

}  // namespace dasall::contracts
