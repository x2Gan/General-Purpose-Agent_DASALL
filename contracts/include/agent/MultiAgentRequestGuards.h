#pragma once

#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agent/MultiAgentRequest.h"
#include "boundary/GuardCommon.h"
#include "boundary/MultiAgentBoundaryGuards.h"
#include "checkpoint/RuntimeBudgetGuards.h"

namespace dasall::contracts {

// Guard result for MultiAgentRequest validation. The structure mirrors the
// established contract-guard result pattern used across the repository.
struct MultiAgentRequestGuardResult {
  bool ok = false;
  std::string_view reason = "multi-agent request validation failed";
};

inline bool multi_agent_request_string_has_non_whitespace_content(
    std::string_view value) {
  for (const unsigned char ch : value) {
    if (!std::isspace(ch)) {
      return true;
    }
  }

  return false;
}

inline bool multi_agent_request_optional_string_has_non_whitespace_content(
    const std::optional<std::string>& value) {
  return !value.has_value() ||
         multi_agent_request_string_has_non_whitespace_content(*value);
}

inline std::string_view multi_agent_request_trimmed_view(
    std::string_view value) {
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

inline bool multi_agent_request_has_duplicate_stop_conditions(
    const std::vector<std::string>& stop_conditions) {
  for (std::size_t index = 0; index < stop_conditions.size(); ++index) {
    for (std::size_t probe = index + 1; probe < stop_conditions.size(); ++probe) {
      if (stop_conditions[index] == stop_conditions[probe]) {
        return true;
      }
    }
  }

  return false;
}

// Validates the required-field rules frozen by WP04-T014:
//   1) parent_request_id and parent_task_id must be present and non-empty.
//   2) goal_fragment and plan_fragment must be present and non-empty.
//   3) collaboration_mode must be present and must not be Unspecified.
inline MultiAgentRequestGuardResult validate_multi_agent_request_required_fields(
    const MultiAgentRequest& request) {
  if (!has_non_empty_value(request.parent_request_id)) {
    return MultiAgentRequestGuardResult{
        .ok = false,
        .reason = "parent_request_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(request.parent_task_id)) {
    return MultiAgentRequestGuardResult{
        .ok = false,
        .reason = "parent_task_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(request.goal_fragment)) {
    return MultiAgentRequestGuardResult{
        .ok = false,
        .reason = "goal_fragment is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(request.plan_fragment)) {
    return MultiAgentRequestGuardResult{
        .ok = false,
        .reason = "plan_fragment is required and must be non-empty",
    };
  }

  if (!request.collaboration_mode.has_value() ||
      *request.collaboration_mode == CollaborationMode::Unspecified) {
    return MultiAgentRequestGuardResult{
        .ok = false,
        .reason =
            "collaboration_mode is required and must not be Unspecified",
    };
  }

  return MultiAgentRequestGuardResult{
      .ok = true,
      .reason = "all required multi-agent request fields present",
  };
}

// Validates WP04-T014 semantic boundary rules on top of required-field checks:
//   1) collaboration_mode must stay within the known enum range.
//   2) parent_request_id and parent_task_id must not collapse to the same
//      identifier string, which would blur request-level and task-level anchors.
//   3) request-shaped aliases such as agent_request or agent_request_payload
//      remain rejectable through the shared ADR-008 boundary guard.
inline MultiAgentRequestGuardResult validate_multi_agent_request_boundary(
    const MultiAgentRequest& request) {
  auto required_result = validate_multi_agent_request_required_fields(request);
  if (!required_result.ok) {
    return required_result;
  }

  const int raw_mode = static_cast<int>(*request.collaboration_mode);
  if (raw_mode < static_cast<int>(CollaborationMode::Unspecified) ||
      raw_mode > static_cast<int>(CollaborationMode::Handoff)) {
    return MultiAgentRequestGuardResult{
        .ok = false,
        .reason = "collaboration_mode value is outside the known enum range",
    };
  }

  if (request.parent_request_id == request.parent_task_id) {
    return MultiAgentRequestGuardResult{
        .ok = false,
        .reason =
            "parent_request_id must not equal parent_task_id because request and task anchors are layered separately",
    };
  }

  return MultiAgentRequestGuardResult{
      .ok = true,
      .reason = "multi-agent request boundary validation passed",
  };
}

// Validates WP04-T015 field-table rules on top of the T014 required/boundary
// guards:
//   1) required string fields must contain non-whitespace content.
//   2) worker_budget_guard, if present, must satisfy RuntimeBudget guards.
//   3) permission_guard, if present, must contain non-whitespace content.
//   4) stop_conditions, if present, must be non-empty, non-whitespace, unique.
//   5) goal_fragment and plan_fragment must not collapse to the same trimmed
//      content.
//   6) Handoff mode requires explicit permission_guard and stop_conditions.
inline MultiAgentRequestGuardResult validate_multi_agent_request_field_rules(
    const MultiAgentRequest& request) {
  const auto boundary_result = validate_multi_agent_request_boundary(request);
  if (!boundary_result.ok) {
    return boundary_result;
  }

  if (!multi_agent_request_string_has_non_whitespace_content(
          *request.parent_request_id)) {
    return MultiAgentRequestGuardResult{
        .ok = false,
        .reason =
            "parent_request_id must contain at least one non-whitespace character",
    };
  }

  if (!multi_agent_request_string_has_non_whitespace_content(
          *request.parent_task_id)) {
    return MultiAgentRequestGuardResult{
        .ok = false,
        .reason =
            "parent_task_id must contain at least one non-whitespace character",
    };
  }

  if (!multi_agent_request_string_has_non_whitespace_content(
          *request.goal_fragment)) {
    return MultiAgentRequestGuardResult{
        .ok = false,
        .reason =
            "goal_fragment must contain at least one non-whitespace character",
    };
  }

  if (!multi_agent_request_string_has_non_whitespace_content(
          *request.plan_fragment)) {
    return MultiAgentRequestGuardResult{
        .ok = false,
        .reason =
            "plan_fragment must contain at least one non-whitespace character",
    };
  }

  if (multi_agent_request_trimmed_view(*request.goal_fragment) ==
      multi_agent_request_trimmed_view(*request.plan_fragment)) {
    return MultiAgentRequestGuardResult{
        .ok = false,
        .reason =
            "goal_fragment and plan_fragment must remain distinct after trimming whitespace",
    };
  }

  if (request.worker_budget_guard.has_value()) {
    const auto budget_result = validate_runtime_budget(*request.worker_budget_guard);
    if (!budget_result.ok) {
      return MultiAgentRequestGuardResult{
          .ok = false,
          .reason =
              "worker_budget_guard must pass nested RuntimeBudget validation",
      };
    }
  }

  if (!multi_agent_request_optional_string_has_non_whitespace_content(
          request.permission_guard)) {
    return MultiAgentRequestGuardResult{
        .ok = false,
        .reason =
            "permission_guard must contain at least one non-whitespace character when present",
    };
  }

  if (request.stop_conditions.has_value()) {
    if (request.stop_conditions->empty()) {
      return MultiAgentRequestGuardResult{
          .ok = false,
          .reason = "stop_conditions must contain at least one item when present",
      };
    }

    for (const auto& stop_condition : *request.stop_conditions) {
      if (!multi_agent_request_string_has_non_whitespace_content(stop_condition)) {
        return MultiAgentRequestGuardResult{
            .ok = false,
            .reason =
                "stop_conditions must not contain empty or whitespace-only items",
        };
      }
    }

    if (multi_agent_request_has_duplicate_stop_conditions(
            *request.stop_conditions)) {
      return MultiAgentRequestGuardResult{
          .ok = false,
          .reason = "stop_conditions must not contain duplicate items",
      };
    }
  }

  if (*request.collaboration_mode == CollaborationMode::Handoff) {
    if (!request.permission_guard.has_value()) {
      return MultiAgentRequestGuardResult{
          .ok = false,
          .reason =
              "permission_guard is required when collaboration_mode is Handoff",
      };
    }

    if (!request.stop_conditions.has_value()) {
      return MultiAgentRequestGuardResult{
          .ok = false,
          .reason =
              "stop_conditions are required when collaboration_mode is Handoff",
      };
    }
  }

  return MultiAgentRequestGuardResult{
      .ok = true,
      .reason = "multi-agent request field rules validation passed",
  };
}

// Validates a candidate top-level field name against the shared ADR-008 request
// boundary guard. This is used by contract tests to prove MultiAgentRequest
// continues to reject AgentRequest-style wrapper aliases.
inline MultiAgentRequestGuardResult validate_multi_agent_request_forbidden_field(
    std::string_view field_name) {
  const auto result = evaluate_multi_agent_request_field_boundary(field_name);
  return MultiAgentRequestGuardResult{
      .ok = result.allowed,
      .reason = result.reason,
  };
}

}  // namespace dasall::contracts