#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include "agent/MultiAgentResult.h"
#include "boundary/GuardCommon.h"
#include "boundary/MultiAgentBoundaryGuards.h"

namespace dasall::contracts {

struct MultiAgentResultGuardResult {
  bool ok = false;
  std::string_view reason = "multi-agent result validation failed";
};

inline bool multi_agent_result_string_has_non_whitespace_content(
    std::string_view value) {
  for (const unsigned char ch : value) {
    if (!std::isspace(ch)) {
      return true;
    }
  }

  return false;
}

inline std::string_view multi_agent_result_trimmed_view(std::string_view value) {
  std::size_t begin = 0;
  while (begin < value.size() &&
         std::isspace(static_cast<unsigned char>(value[begin]))) {
    ++begin;
  }

  std::size_t end = value.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }

  return value.substr(begin, end - begin);
}

inline bool multi_agent_result_has_duplicate_items(
    const std::vector<std::string>& values) {
  for (std::size_t index = 0; index < values.size(); ++index) {
    const auto current = multi_agent_result_trimmed_view(values[index]);
    for (std::size_t probe = index + 1; probe < values.size(); ++probe) {
      if (current == multi_agent_result_trimmed_view(values[probe])) {
        return true;
      }
    }
  }

  return false;
}

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

// Validates WP04-T017 field-table rules on top of the T016 required/boundary
// guards:
//   1) String fields must contain non-whitespace content.
//   2) Vector fields must not contain whitespace-only items.
//   3) Aggregation vectors must not contain duplicate items.
//   4) merged_result and recommended_next_action must remain semantically
//      distinct after trimming whitespace.
inline MultiAgentResultGuardResult validate_multi_agent_result_field_rules(
    const MultiAgentResult& result) {
  const auto boundary_result = validate_multi_agent_result_boundary(result);
  if (!boundary_result.ok) {
    return boundary_result;
  }

  for (const auto& subtask_result : *result.subtask_results) {
    if (!multi_agent_result_string_has_non_whitespace_content(subtask_result)) {
      return MultiAgentResultGuardResult{
          .ok = false,
          .reason =
              "subtask_results must not contain empty or whitespace-only items",
      };
    }
  }

  if (multi_agent_result_has_duplicate_items(*result.subtask_results)) {
    return MultiAgentResultGuardResult{
        .ok = false,
        .reason = "subtask_results must not contain duplicate items",
    };
  }

  if (!multi_agent_result_string_has_non_whitespace_content(*result.merged_result)) {
    return MultiAgentResultGuardResult{
        .ok = false,
        .reason = "merged_result must contain at least one non-whitespace character",
    };
  }

  if (!multi_agent_result_string_has_non_whitespace_content(
          *result.recommended_next_action)) {
    return MultiAgentResultGuardResult{
        .ok = false,
        .reason =
            "recommended_next_action must contain at least one non-whitespace character",
    };
  }

  if (multi_agent_result_trimmed_view(*result.merged_result) ==
      multi_agent_result_trimmed_view(*result.recommended_next_action)) {
    return MultiAgentResultGuardResult{
        .ok = false,
        .reason =
            "merged_result and recommended_next_action must remain distinct after trimming whitespace",
    };
  }

  if (result.conflicts.has_value()) {
    for (const auto& conflict : *result.conflicts) {
      if (!multi_agent_result_string_has_non_whitespace_content(conflict)) {
        return MultiAgentResultGuardResult{
            .ok = false,
            .reason =
                "conflicts must not contain empty or whitespace-only items",
        };
      }
    }

    if (multi_agent_result_has_duplicate_items(*result.conflicts)) {
      return MultiAgentResultGuardResult{
          .ok = false,
          .reason = "conflicts must not contain duplicate items",
      };
    }
  }

  if (result.worker_trace_refs.has_value()) {
    for (const auto& worker_trace_ref : *result.worker_trace_refs) {
      if (!multi_agent_result_string_has_non_whitespace_content(
              worker_trace_ref)) {
        return MultiAgentResultGuardResult{
            .ok = false,
            .reason =
                "worker_trace_refs must not contain empty or whitespace-only items",
        };
      }
    }

    if (multi_agent_result_has_duplicate_items(*result.worker_trace_refs)) {
      return MultiAgentResultGuardResult{
          .ok = false,
          .reason = "worker_trace_refs must not contain duplicate items",
      };
    }
  }

  if (result.failure_summary.has_value() &&
      !multi_agent_result_string_has_non_whitespace_content(
          *result.failure_summary)) {
    return MultiAgentResultGuardResult{
        .ok = false,
        .reason =
            "failure_summary must contain at least one non-whitespace character when present",
    };
  }

  return MultiAgentResultGuardResult{
      .ok = true,
      .reason = "multi-agent result field rules validation passed",
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