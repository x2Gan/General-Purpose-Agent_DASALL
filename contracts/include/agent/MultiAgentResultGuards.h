#pragma once

#include <string_view>

#include "agent/MultiAgentResult.h"
#include "boundary/GuardCommon.h"
#include "boundary/MultiAgentBoundaryGuards.h"

namespace dasall::contracts {

struct MultiAgentResultGuardResult {
  bool ok = false;
  std::string_view reason = "multi-agent result validation failed";
};

// Validates the required-field rules frozen by WP04-T016:
//   1) subtask_results must be present and contain at least one item.
//   2) merged_result must be present and non-empty.
//   3) recommended_next_action must be present and non-empty.
inline MultiAgentResultGuardResult validate_multi_agent_result_required_fields(
    const MultiAgentResult& result) {
  if (!result.subtask_results.has_value() || result.subtask_results->empty()) {
    return MultiAgentResultGuardResult{
        .ok = false,
        .reason = "subtask_results are required and must contain at least one item",
    };
  }

  if (!has_non_empty_value(result.merged_result)) {
    return MultiAgentResultGuardResult{
        .ok = false,
        .reason = "merged_result is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(result.recommended_next_action)) {
    return MultiAgentResultGuardResult{
        .ok = false,
        .reason = "recommended_next_action is required and must be non-empty",
    };
  }

  return MultiAgentResultGuardResult{
      .ok = true,
      .reason = "all required multi-agent result fields present",
  };
}

// Validates WP04-T016 semantic boundary rules on top of required-field checks:
//   1) subtask_results items must be non-empty strings.
//   2) conflicts, if present, must be non-empty vector with non-empty items.
//   3) worker_trace_refs, if present, must be non-empty vector with non-empty items.
//   4) failure_summary, if present, must be non-empty.
inline MultiAgentResultGuardResult validate_multi_agent_result_boundary(
    const MultiAgentResult& result) {
  const auto required_result = validate_multi_agent_result_required_fields(result);
  if (!required_result.ok) {
    return required_result;
  }

  for (const auto& subtask_result : *result.subtask_results) {
    if (subtask_result.empty()) {
      return MultiAgentResultGuardResult{
          .ok = false,
          .reason = "subtask_results must not contain empty items",
      };
    }
  }

  if (result.conflicts.has_value()) {
    if (result.conflicts->empty()) {
      return MultiAgentResultGuardResult{
          .ok = false,
          .reason = "conflicts must contain at least one item when present",
      };
    }

    for (const auto& conflict : *result.conflicts) {
      if (conflict.empty()) {
        return MultiAgentResultGuardResult{
            .ok = false,
            .reason = "conflicts must not contain empty strings",
        };
      }
    }
  }

  if (result.worker_trace_refs.has_value()) {
    if (result.worker_trace_refs->empty()) {
      return MultiAgentResultGuardResult{
          .ok = false,
          .reason = "worker_trace_refs must contain at least one item when present",
      };
    }

    for (const auto& worker_trace_ref : *result.worker_trace_refs) {
      if (worker_trace_ref.empty()) {
        return MultiAgentResultGuardResult{
            .ok = false,
            .reason = "worker_trace_refs must not contain empty strings",
        };
      }
    }
  }

  if (result.failure_summary.has_value() && result.failure_summary->empty()) {
    return MultiAgentResultGuardResult{
        .ok = false,
        .reason = "failure_summary must be non-empty when present",
    };
  }

  return MultiAgentResultGuardResult{
      .ok = true,
      .reason = "multi-agent result boundary validation passed",
  };
}

// Validates a candidate top-level field name against the shared ADR-008 result
// boundary guard. This is used by contract tests to prove MultiAgentResult
// continues to reject AgentResult-style replacement aliases.
inline MultiAgentResultGuardResult validate_multi_agent_result_forbidden_field(
    std::string_view field_name) {
  const auto result = evaluate_multi_agent_result_field_boundary(field_name);
  return MultiAgentResultGuardResult{
      .ok = result.allowed,
      .reason = result.reason,
  };
}

}  // namespace dasall::contracts