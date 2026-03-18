#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "error/ErrorInfo.h"

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// AgentResultStatus enumerates the terminal states that an AgentResult
// can express.
//
// Covers normal completion, failure, degraded output, cancellation,
// and timeout scenarios as required by architecture §5.1, §6.10:
//   - Unspecified         : sentinel; rejected by guards.
//   - Completed           : task completed normally (task_completed=true).
//   - Failed              : task failed (ErrorInfo populated).
//   - PartiallyCompleted  : degraded output (architecture §6.10 degrade).
//   - Cancelled           : user or system cancellation.
//   - Timeout             : exceeded RuntimeBudget max_latency_ms.
//
// Enum range: [0, 5].  Guards reject values outside this range.
// ---------------------------------------------------------------------------
enum class AgentResultStatus : int {
  Unspecified = 0,
  Completed = 1,
  Failed = 2,
  PartiallyCompleted = 3,
  Cancelled = 4,
  Timeout = 5,
};

// ---------------------------------------------------------------------------
// AgentResult is the unified output contract object for a single Agent
// request lifecycle, as defined by WP03-T014 semantic boundary.
//
// Main-flow chain position (WP03-T001):
//   AgentRequest -> GoalContract -> ContextPacket -> Observation
//     -> ObservationDigest -> BeliefState -> Checkpoint -> [AgentResult]
//
// Core responsibility (architecture §3.8.1, §3.8.4, §5.1):
//   AgentResult is the unified exit point for an Agent.  Beyond text
//   reply, it must include the final status, structured artifacts, and
//   audit references.  All execution paths — single agent, multi-agent,
//   tool results — must converge into AgentResult.
//
// Architecture §5.1 required fields (pseudocode mapping):
//   1. ResultCode code          -> result_code (int32, WP-02 value range)
//   2. string response_text     -> response_text
//   3. JsonObject structured_payload -> structured_payload (string ref)
//   4. bool task_completed       -> task_completed
//   5. ErrorInfo error           -> error_info (optional)
//
// Additional required fields from §3.8.1 (audit references):
//   6. result_id                -> unique result identifier
//   7. status (AgentResultStatus) -> fine-grained terminal state
//   8. created_at               -> result generation timestamp
//
// Who produces AgentResult (architecture §4.1, §11.1, ADR-008):
//   - ResponseBuilder: assembles AgentResult from ContextPacket + Plan
//     + Observation + decision (architecture §11.1 pseudocode).
//   - AgentOrchestrator: **sole authority** for submitting the final
//     AgentResult (ADR-008 §7.2 #2).  No Worker or MultiAgentCoordinator
//     may directly produce a user-facing AgentResult.
//
// Consumers (architecture §4.1, Blueprint §5.7.3):
//   - Agent Facade: unified exit interface.
//   - AccessGateway: publishes result to user-facing channel.
//   - Session Manager: records session terminal state.
//   - Audit system: traces via request_id + trace_id.
//
// What AgentResult is NOT (WP03-T014 frozen boundaries):
//   - NOT a cognitive state: confirmed_facts, hypotheses, assumptions,
//     evidence_refs, confidence belong to BeliefState (WP03-T009).
//   - NOT an execution record: payload, success, side_effects,
//     tool_call_id, duration_ms belong to Observation (WP03-T006).
//   - NOT a reasoning summary: summary, key_facts, citations belong
//     to ObservationDigest (WP03-T008).
//   - NOT a recovery snapshot: working_memory_snapshot, pending_action,
//     step_id, retry_count belong to Checkpoint (WP03-T012).
//   - NOT a prompt/context object: user_turn, recent_history,
//     retrieval_evidence belong to ContextPacket (WP03-T010).
//   - NOT a plan or decision: plan_graph, step_list, action_decision
//     belong to Planner/Reasoner.
//   - NOT a message rendering: final_messages, rendered_prompt belong
//     to PromptComposeResult (ADR-006).
//   - NOT a multi-agent detail: subtask_results, worker_task_list,
//     lease_id, delegation_policy belong to Multi-Agent (ADR-008).
//   - NOT a FSM internal: fsm_state, state_transition_log,
//     runtime_budget_consumed belong to Runtime internals.
//   - NOT an entry request: attachments, request_channel, user_input
//     belong to AgentRequest (WP03-T002).
//
// Forbidden fields (WP03-T014 frozen boundaries):
//   - Cognitive state: confirmed_facts, hypotheses, assumptions,
//     evidence_refs, confidence.
//   - Execution detail: payload, success, side_effects, tool_call_id,
//     worker_task_id, duration_ms.
//   - Digest content: summary, key_facts, citations, omitted_details.
//   - Recovery state: working_memory_snapshot, pending_action,
//     step_id, retry_count.
//   - Context slots: user_turn, recent_history, retrieval_evidence,
//     summary_memory, active_tools, policy_digest.
//   - Message rendering: final_messages, rendered_prompt, prompt_bundle,
//     system_instructions, prompt_template, few_shots, output_schema.
//   - Model vendor: model_provider_args, vendor_tool_schema.
//   - Plan/decision: plan_graph, step_list, action_decision, next_step.
//   - Multi-Agent: worker_task_list, lease_id, delegation_policy,
//     subtask_results, merged_result, conflicts.
//   - FSM internal: fsm_state, state_transition_log,
//     runtime_budget_consumed.
//   - Entry request: attachments, request_channel, user_input.
// ---------------------------------------------------------------------------
struct AgentResult {
  // -----------------------------------------------------------------------
  // Required fields (WP03-T014, 6 items — architecture §5.1 + §3.8.1)
  // -----------------------------------------------------------------------

  // Unique result identifier, supporting audit trail and cross-system
  // traceability (architecture §3.8.1 "审计引用").
  std::optional<std::string> result_id;

  // Fine-grained terminal status of the agent execution.
  // Must be a known AgentResultStatus value (not Unspecified).
  // Complements task_completed with richer semantics (6 states vs 2).
  std::optional<AgentResultStatus> status;

  // WP-02 frozen result code (numeric).  Uses int32 instead of the
  // ResultCode enum to support future code extensions while maintaining
  // backward compatibility.  Category classification is performed via
  // classify_result_code_segment() from ResultCode.h.
  // Architecture §5.1 field 1: "ResultCode code".
  std::optional<std::int32_t> result_code;

  // Human-readable text reply to the user.
  // Architecture §5.1 field 2: "string response_text".
  // Empty string is allowed — some structured-output scenarios may not
  // produce a text reply.  The field must be present (has_value) to
  // indicate "we have generated a response", even when it is empty.
  std::optional<std::string> response_text;

  // Whether the task was completed successfully.
  // Architecture §5.1 field 4: "bool task_completed".
  // Retained for architecture backward compatibility alongside status.
  // true = Completed, false = Failed/PartiallyCompleted/Cancelled/Timeout.
  std::optional<bool> task_completed;

  // Result generation timestamp in milliseconds.
  // Used for temporal ordering, freshness checks, and audit.
  // If present, must be positive.
  std::optional<std::int64_t> created_at;

  // -----------------------------------------------------------------------
  // Optional fields (WP03-T014, 7 items)
  // -----------------------------------------------------------------------

  // Correlation back to the originating AgentRequest.request_id.
  // Supports end-to-end audit trail (architecture §3.8.1 "审计引用").
  // If present, must be non-empty.
  std::optional<std::string> request_id;

  // Trace chain identifier for log/event/audit alignment.
  // Supports cross-system traceability (architecture §3.8.1 "审计引用").
  // If present, must be non-empty.
  std::optional<std::string> trace_id;

  // Serialized structured artifact (architecture §5.1 "JsonObject
  // structured_payload").  Uses string for serialization flexibility —
  // JsonObject is not a contracts-layer frozen type, and content
  // format is defined by business layer.
  // If present, must be non-empty.
  std::optional<std::string> structured_payload;

  // Standardized error information when the execution has failed.
  // Directly reuses WP-02 frozen ErrorInfo (failure_type, retryable,
  // safe_to_replan, details, source_ref).
  // Architecture §5.1 field 5: "ErrorInfo error".
  // Optional because success scenarios do not carry error details.
  std::optional<ErrorInfo> error_info;

  // Reference to the final Checkpoint at completion time.
  // Serves as "completion proof" — the Checkpoint struct is a separate
  // contract object (WP03-T012); AgentResult holds a reference
  // identifier, not the full embedded structure.
  // If present, must be non-empty.
  std::optional<std::string> checkpoint_ref;

  // Correlation to the current GoalContract.goal_id.  In multi-goal
  // scenarios, identifies which goal this result pertains to.
  // If present, must be non-empty.
  std::optional<std::string> goal_id;

  // Retrieval/audit tags.  Do not carry execution control signals.
  // Semantically consistent with other contract object tags
  // (AgentRequest, GoalContract, BeliefState, ContextPacket, Checkpoint).
  std::optional<std::vector<std::string>> tags;
};

}  // namespace dasall::contracts
