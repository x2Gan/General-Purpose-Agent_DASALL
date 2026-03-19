#pragma once

#include <optional>
#include <string>
#include <vector>

#include "checkpoint/RuntimeBudget.h"

namespace dasall::contracts {

// CollaborationMode freezes the orchestrator-decided collaboration mode for a
// multi-agent subdomain request. The explicit Unspecified sentinel follows the
// repository enum lifecycle rule so guards can detect incomplete construction.
enum class CollaborationMode {
  Unspecified = 0,
  Sequential = 1,
  Concurrent = 2,
  Handoff = 3,
};

// MultiAgentRequest is the collaboration-subdomain request object introduced by
// WP04-T014. It is produced by AgentOrchestrator after the top-level request has
// already been normalized and admitted, and consumed by MultiAgentCoordinator to
// build a worker-task graph.
//
// Semantic boundary (WP04-T014 frozen):
//   Allowed:
//     1. Parent anchors: parent_request_id, parent_task_id
//     2. Collaboration scope: goal_fragment, plan_fragment, collaboration_mode
//     3. Collaboration guards: worker_budget_guard, permission_guard,
//        stop_conditions
//
//   Forbidden:
//     - Top-level AgentRequest entry semantics (user_input, request_channel,
//       domain_context, constraint_set, approval_policy_hint)
//     - Global runtime/session/final-result semantics (session_id,
//       checkpoint_ref, final_agent_response, agent_result)
//     - Worker execution-unit or lease internals (worker_type, allowed_tools,
//       lease_id, deadline_at)
//
// Required fields intentionally use std::optional so guards can return precise
// diagnostics, matching the established contract style in WP03/WP04 objects.
struct MultiAgentRequest {
  // -----------------------------------------------------------------------
  // Required fields (WP04-T014, 5 items)
  // -----------------------------------------------------------------------

  // Stable anchor to the top-level AgentRequest that entered the global runtime
  // lifecycle before collaboration mode was selected.
  std::optional<std::string> parent_request_id;

  // Stable anchor to the orchestrator-owned parent task or collaboration root
  // task in the top-level task graph.
  std::optional<std::string> parent_task_id;

  // The collaboration-local goal fragment already narrowed by the global
  // orchestrator. This is not a full user request payload.
  std::optional<std::string> goal_fragment;

  // The collaboration-local execution plan fragment that downstream worker-task
  // planning can refine. This is not a full planner state object.
  std::optional<std::string> plan_fragment;

  // The collaboration strategy selected by the orchestrator for the current
  // subdomain request.
  std::optional<CollaborationMode> collaboration_mode;

  // -----------------------------------------------------------------------
  // Optional fields (WP04-T014, 3 items)
  // -----------------------------------------------------------------------

  // Worker-scope budget upper bound inherited from the top-level runtime
  // budget. Reuses RuntimeBudget rather than inventing a second budget schema.
  std::optional<RuntimeBudget> worker_budget_guard;

  // Permission-domain summary describing what the collaboration stage is
  // allowed to access or invoke. Detailed rule normalization is deferred to
  // WP04-T015 field-table work.
  std::optional<std::string> permission_guard;

  // Collaboration exit conditions owned by the current subdomain request. The
  // field describes only local stop criteria and does not imply final request
  // completion.
  std::optional<std::vector<std::string>> stop_conditions;
};

}  // namespace dasall::contracts