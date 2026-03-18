#pragma once

#include <string_view>

#include "boundary/GuardCommon.h"
#include "observation/Observation.h"
#include "observation/ObservationSource.h"

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// Guard result for ObservationSource correlation validation, following the
// same pattern as ObservationGuardResult (WP03-T006).
// ---------------------------------------------------------------------------
struct ObservationSourceGuardResult {
  bool ok = false;
  std::string_view reason = "observation source correlation validation failed";
};

// ---------------------------------------------------------------------------
// Layer 3: Source→Correlation field consistency validation (WP03-T007-B).
//
// This guard runs AFTER Layer 1 (required fields) and Layer 2 (boundary)
// have passed. It validates that the Observation's source enum value is
// consistent with the presence/absence of correlation fields.
//
// Rules (WP03-T007 §7):
//   R1: source=ToolExecution  → tool_call_id MUST be present and non-empty.
//   R2: source=WorkerAgent    → worker_task_id MUST be present and non-empty.
//   R3: source=ToolExecution  → worker_task_id MUST NOT be present.
//   R4: source=WorkerAgent    → tool_call_id MUST NOT be present.
//   R5: source=HumanFeedback  → tool_call_id MUST NOT be present.
//   R6: source=HumanFeedback  → worker_task_id MUST NOT be present.
//   R7: Any correlation field, if present, must be non-empty.
//
// Retrieval source has no dedicated must-exist correlation field.
// Retrieval only enforces R7 (non-empty when present) and rejects
// worker_task_id (not a worker channel).
// ---------------------------------------------------------------------------
inline ObservationSourceGuardResult validate_observation_source_correlation(
    const Observation& obs) {

  // Precondition: source must be present and not Unspecified.
  // Layer 1 already guarantees this. We defensively check anyway.
  if (!obs.source.has_value() ||
      *obs.source == ObservationSource::Unspecified) {
    return ObservationSourceGuardResult{
        .ok = false,
        .reason = "source must be present and not Unspecified before "
                  "correlation validation",
    };
  }

  const auto src = *obs.source;

  // -------------------------------------------------------------------
  // R7: Any correlation field, if present, must be non-empty.
  //     (Applies to all source types.)
  // -------------------------------------------------------------------
  if (obs.tool_call_id.has_value() && obs.tool_call_id->empty()) {
    return ObservationSourceGuardResult{
        .ok = false,
        .reason = "tool_call_id must be non-empty when present",
    };
  }
  if (obs.worker_task_id.has_value() && obs.worker_task_id->empty()) {
    return ObservationSourceGuardResult{
        .ok = false,
        .reason = "worker_task_id must be non-empty when present",
    };
  }
  if (obs.request_id.has_value() && obs.request_id->empty()) {
    return ObservationSourceGuardResult{
        .ok = false,
        .reason = "request_id must be non-empty when present",
    };
  }
  if (obs.goal_id.has_value() && obs.goal_id->empty()) {
    return ObservationSourceGuardResult{
        .ok = false,
        .reason = "goal_id must be non-empty when present",
    };
  }

  // -------------------------------------------------------------------
  // Source-specific rules.
  // -------------------------------------------------------------------
  switch (src) {
    case ObservationSource::ToolExecution: {
      // R1: tool_call_id MUST be present and non-empty.
      if (!has_non_empty_value(obs.tool_call_id)) {
        return ObservationSourceGuardResult{
            .ok = false,
            .reason = "tool_call_id is required when source is ToolExecution",
        };
      }
      // R3: worker_task_id MUST NOT be present.
      if (obs.worker_task_id.has_value()) {
        return ObservationSourceGuardResult{
            .ok = false,
            .reason = "worker_task_id must not be present when source is "
                      "ToolExecution",
        };
      }
      break;
    }

    case ObservationSource::WorkerAgent: {
      // R2: worker_task_id MUST be present and non-empty.
      if (!has_non_empty_value(obs.worker_task_id)) {
        return ObservationSourceGuardResult{
            .ok = false,
            .reason = "worker_task_id is required when source is WorkerAgent",
        };
      }
      // R4: tool_call_id MUST NOT be present.
      if (obs.tool_call_id.has_value()) {
        return ObservationSourceGuardResult{
            .ok = false,
            .reason = "tool_call_id must not be present when source is "
                      "WorkerAgent",
        };
      }
      break;
    }

    case ObservationSource::Retrieval: {
      // Retrieval has no dedicated must-exist correlation field.
      // But worker_task_id should not be present (not a worker channel).
      if (obs.worker_task_id.has_value()) {
        return ObservationSourceGuardResult{
            .ok = false,
            .reason = "worker_task_id must not be present when source is "
                      "Retrieval",
        };
      }
      break;
    }

    case ObservationSource::HumanFeedback: {
      // R5: tool_call_id MUST NOT be present.
      if (obs.tool_call_id.has_value()) {
        return ObservationSourceGuardResult{
            .ok = false,
            .reason = "tool_call_id must not be present when source is "
                      "HumanFeedback",
        };
      }
      // R6: worker_task_id MUST NOT be present.
      if (obs.worker_task_id.has_value()) {
        return ObservationSourceGuardResult{
            .ok = false,
            .reason = "worker_task_id must not be present when source is "
                      "HumanFeedback",
        };
      }
      break;
    }

    case ObservationSource::Unspecified:
      // Already rejected above; unreachable.
      break;
  }

  return ObservationSourceGuardResult{
      .ok = true,
      .reason = "observation source correlation validation passed",
  };
}

}  // namespace dasall::contracts
