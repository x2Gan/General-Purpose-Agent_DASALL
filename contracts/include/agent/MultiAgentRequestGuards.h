#pragma once

#include <string_view>

#include "agent/MultiAgentRequest.h"
#include "boundary/GuardCommon.h"
#include "boundary/MultiAgentBoundaryGuards.h"

namespace dasall::contracts {

// Guard result for MultiAgentRequest validation. The structure mirrors the
// established contract-guard result pattern used across the repository.
struct MultiAgentRequestGuardResult {
  bool ok = false;
  std::string_view reason = "multi-agent request validation failed";
};

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