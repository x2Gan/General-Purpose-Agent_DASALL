#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dasall::contracts {

// WorkerTask is the execution-unit object introduced by WP04-T018.
// It is created by MultiAgentCoordinator after a MultiAgentRequest has been
// admitted and decomposed into worker-level subtasks.
//
// Semantic boundary (WP04-T018 frozen):
//   Allowed:
//     1. Execution identity: task_id, parent_task_id
//     2. Lease anchor: lease_id
//     3. Worker execution routing: worker_type, allowed_tools
//     4. Local execution control: timeout_ms, idempotency_key
//
//   Forbidden:
//     - Global session / FSM control state (session_id, global_fsm_state,
//       checkpoint_ref)
//     - Top-level final result semantics (agent_result, final_agent_response)
//     - WorkerLease metadata (renewal_deadline, release_reason, lease_state)
struct WorkerTask {
  // -----------------------------------------------------------------------
  // Required fields (WP04-T018, 6 items)
  // -----------------------------------------------------------------------

  // Stable identity for the worker execution unit itself.
  std::optional<std::string> task_id;

  // Parent anchor pointing back to the orchestrator-owned task graph.
  std::optional<std::string> parent_task_id;

  // Lease anchor for the worker execution slot. Detailed lease semantics are
  // defined later by WorkerLease.
  std::optional<std::string> lease_id;

  // Worker capability or execution role selected for the current subtask.
  std::optional<std::string> worker_type;

  // Tool names permitted for this execution unit.
  std::optional<std::vector<std::string>> allowed_tools;

  // Local execution timeout in milliseconds.
  std::optional<std::uint32_t> timeout_ms;

  // -----------------------------------------------------------------------
  // Optional fields (WP04-T018, 1 item)
  // -----------------------------------------------------------------------

  // Idempotency anchor for replay-safe or compensable worker executions.
  std::optional<std::string> idempotency_key;
};

}  // namespace dasall::contracts