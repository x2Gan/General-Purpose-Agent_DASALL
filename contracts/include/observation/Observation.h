#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "error/ErrorInfo.h"
#include "observation/ObservationSource.h"

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// Observation is the unified folding container for all execution outputs in
// the single-Agent main flow, as defined by WP03-T006 semantic boundary.
//
// Core responsibility (architecture 3.8.2):
//   The system folds tool results, retrieval results, human feedback, and
//   worker agent outputs into a single Observation structure with ErrorInfo.
//
// Six semantic roles (WP03-T006 frozen):
//   1. Source identification:  Which production channel generated this output.
//   2. Success status:         Whether the execution succeeded.
//   3. Result payload:         The raw structured output data.
//   4. Error information:      Structured error details on failure.
//   5. Side-effect declaration: Irreversible changes caused by execution.
//   6. Correlation tracking:   Links back to the originating execution request.
//
// Semantic boundary (WP03-T006 frozen):
//   Allowed:
//     1. Observation identity and source: observation_id, source, created_at.
//     2. Execution result: success, payload.
//     3. Error: error (reuses WP02 ErrorInfo).
//     4. Side effects: side_effects.
//     5. Correlation: tool_call_id, worker_task_id, request_id, goal_id.
//     6. Metadata: duration_ms, tags.
//
//   Forbidden (WP03-T006 frozen):
//     - Plan/decision fields (plan_graph, step_list, action_decision, next_step)
//     - Runtime internal state (fsm_state, retry_counters, backoff_ms)
//     - Recovery control fields (checkpoint_ref, recovery_action, replan_trigger)
//     - Message rendering fields (final_messages, rendered_prompt, prompt_bundle)
//     - Provider private fields (model_provider_args, vendor_tool_schema)
//     - Digest/reasoning fields (summary, key_facts, confidence → ObservationDigest)
//
// Consumers:
//   - Reasoner: reads latest_observation for next-step decision.
//   - ReflectionEngine: analyzes failed Observations for root cause.
//   - RecoveryManager: receives latest_observation for recovery decisions.
//   - Memory: persists Observation via write_observation().
//   - ObservationDigest builder: derives reasoning-friendly digest.
//   - ContextOrchestrator: consumes Observation indirectly via Digest.
//   - Checkpoint: snapshots Observation reference for recovery.
// ---------------------------------------------------------------------------
struct Observation {
  // -----------------------------------------------------------------------
  // Required fields (WP03-T006, 5 items)
  // -----------------------------------------------------------------------

  // Observation-level unique identifier, generated at production time and
  // propagated throughout the observation lifecycle. Used as the reference
  // key by ReflectionDecision.relevant_observation_refs and Memory writes.
  // References WP02-T009 identification rules.
  std::optional<std::string> observation_id;

  // Source channel that produced this Observation. References the
  // ObservationSource enum. Does not carry execution details.
  std::optional<ObservationSource> source;

  // Whether the execution that produced this Observation succeeded.
  // true = success, false = failure (error field should be present).
  std::optional<bool> success;

  // Execution result payload as a structured string (typically JSON).
  // Contains the raw output data for programmatic consumption.
  // Does not contain message rendering or digest summaries.
  std::optional<std::string> payload;

  // Observation creation timestamp in milliseconds (WP02-T010).
  // Serves as the temporal baseline for audit and memory ordering.
  std::optional<std::int64_t> created_at;

  // -----------------------------------------------------------------------
  // Optional fields (WP03-T006, 8 items)
  // -----------------------------------------------------------------------

  // Structured error information on execution failure. Reuses the WP02
  // frozen ErrorInfo contract object. Should be present when success=false,
  // absent when success=true.
  std::optional<ErrorInfo> error;

  // Declarative list of irreversible changes caused by the execution.
  // Used for idempotency checking and compensation decisions.
  // Each entry is a descriptive string, not executable logic.
  std::optional<std::vector<std::string>> side_effects;

  // Correlation identifier linking back to the tool call that produced
  // this Observation. Relevant when source=ToolExecution or McpRemote.
  std::optional<std::string> tool_call_id;

  // Correlation identifier linking back to the Worker Agent sub-task.
  // Relevant when source=WorkerAgent.
  std::optional<std::string> worker_task_id;

  // Correlation back to the originating AgentRequest.request_id.
  std::optional<std::string> request_id;

  // Correlation back to the GoalContract.goal_id being pursued.
  std::optional<std::string> goal_id;

  // Execution duration in milliseconds. If present, must be positive.
  std::optional<std::int64_t> duration_ms;

  // Retrieval/audit tags. Do not carry execution control signals.
  std::optional<std::vector<std::string>> tags;
};

}  // namespace dasall::contracts
