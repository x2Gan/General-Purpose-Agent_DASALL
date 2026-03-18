#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// CheckpointState enumerates the FSM states that a Checkpoint can capture.
//
// Covers all four mid-execution interruption scenarios from architecture
// §6.10 plus normal and terminal states:
//   - Unspecified  : sentinel; rejected by guards.
//   - Running      : normal execution in progress.
//   - Paused       : waiting for user clarification (§6.10 scenario 1).
//   - WaitingConfirm : waiting for high-risk action confirmation (§6.10 scenario 2).
//   - WaitingTool  : waiting for async tool or sub-agent return (§6.10 scenario 3).
//   - Failed       : execution failed, recovery pending (ADR-007 §4).
//   - Succeeded    : execution completed successfully.
//
// Enum range: [0, 6].  Guards reject values outside this range.
// ---------------------------------------------------------------------------
enum class CheckpointState : int {
  Unspecified = 0,
  Running = 1,
  Paused = 2,
  WaitingConfirm = 3,
  WaitingTool = 4,
  Failed = 5,
  Succeeded = 6,
};

// ---------------------------------------------------------------------------
// Checkpoint is the minimal recovery state object persisted by Runtime
// (Agent Control Plane) before and after each FSM state transition,
// as defined by WP03-T012 semantic boundary.
//
// Main-flow chain position (WP03-T001):
//   AgentRequest -> GoalContract -> ContextPacket -> Observation
//     -> ObservationDigest -> BeliefState -> [Checkpoint] -> AgentResult
//
// Core responsibility (architecture §3.8.3, §3.4.2, §6.10):
//   Checkpoint records the minimum information needed for RecoveryManager
//   to resume execution after mid-execution interruption, rather than
//   restarting from scratch.  It answers three questions:
//     1. Which actions have already produced side-effects?
//     2. Which actions are still waiting for confirmation or completion?
//     3. Which states can be safely replayed?
//
// Architecture §3.8.3 required information (5 items):
//   1. "当前状态"               -> state (CheckpointState)
//   2. "当前步骤或 step_id"      -> step_id
//   3. "working_memory_snapshot" -> working_memory_snapshot
//   4. "retry_counters"          -> retry_count (optional, see design note)
//   5. "pending_action"          -> pending_action
//
// What Checkpoint is NOT (WP03-T012 frozen, freezing plan §2-6):
//   - NOT a cognitive state: confirmed_facts, hypotheses, assumptions,
//     evidence_refs, confidence belong to BeliefState (WP03-T009).
//   - NOT an execution record: payload, success, error, side_effects,
//     tool_call_id, duration_ms belong to Observation (WP03-T006).
//   - NOT a reasoning summary: summary, key_facts, citations belong
//     to ObservationDigest (WP03-T008).
//   - NOT a prompt/context object: user_turn, recent_history,
//     retrieval_evidence belong to ContextPacket (WP03-T010).
//   - NOT an infinite working-memory dump: must only express the
//     minimum state needed for recovery (freezing plan §2-6).
//   - NOT a plan or decision: plan_graph, step_list, action_decision
//     belong to Planner/Reasoner.
//   - NOT a message rendering: final_messages, rendered_prompt belong
//     to PromptComposeResult (ADR-006).
//   - NOT a multi-agent control: worker_task_list, lease_id,
//     delegation_policy belong to Multi-Agent coordination (ADR-008).
//
// Producer (architecture §3.4.2, ADR-007 §4):
//   - Runtime (Agent Control Plane): sole producer. Persists checkpoint
//     before/after each FSM state transition.
//
// Consumers (ADR-007 §5.2/§5.3, architecture §6.10):
//   - RecoveryManager: reads checkpoint + retry info + pending_action
//     for recovery decision (ADR-007 §5.2).
//   - ReflectionEngine: reads checkpoint for failure attribution context
//     (ADR-007 §3.2).
//   - ContextOrchestrator: may reference checkpoint state for context
//     assembly.
//   - AgentResult: may reference final checkpoint as completion proof.
//
// Forbidden fields (WP03-T012 frozen boundaries):
//   - Cognitive state: confirmed_facts, hypotheses, assumptions,
//     evidence_refs, confidence.
//   - Execution detail: payload, success, error, side_effects,
//     tool_call_id, worker_task_id, duration_ms.
//   - Digest content: summary, key_facts, citations, omitted_details.
//   - Context slots: user_turn, recent_history, retrieval_evidence,
//     summary_memory, active_tools, policy_digest.
//   - Message rendering: final_messages, rendered_prompt, prompt_bundle,
//     system_instructions, prompt_template, few_shots, output_schema.
//   - Model vendor: model_provider_args, vendor_tool_schema.
//   - Plan/decision: plan_graph, step_list, action_decision, next_step.
//   - Multi-Agent: worker_task_list, lease_id, delegation_policy.
//   - Entry request: attachments, request_channel.
// ---------------------------------------------------------------------------
struct Checkpoint {
  // -----------------------------------------------------------------------
  // Required fields (WP03-T012, 5 items — architecture §3.8.3)
  // -----------------------------------------------------------------------

  // Unique checkpoint identifier, supporting checkpoint_ref references
  // from RecoveryOutcome (ADR-007 §5.3) and audit trail.
  std::optional<std::string> checkpoint_id;

  // Current FSM state at the time of checkpoint creation.
  // Must be a known CheckpointState value (not Unspecified).
  // Architecture §3.8.3 item 1: "当前状态".
  std::optional<CheckpointState> state;

  // Execution progress identifier — the step or action being executed
  // when this checkpoint was taken.
  // Architecture §3.8.3 item 2: "当前步骤或 step_id".
  std::optional<std::string> step_id;

  // Serialized reference or summary of the working memory state.
  // This is intentionally a string (reference/summary), NOT a full
  // key-value dump — per freezing plan §2-6: "Checkpoint must only
  // express the minimum state needed for recovery, not become an
  // infinite working-memory container."
  // Architecture §3.8.3 item 3: "working_memory_snapshot".
  std::optional<std::string> working_memory_snapshot;

  // Description of the action currently pending confirmation, completion,
  // or acknowledgment.  May be an empty string when no action is pending
  // (e.g., Running state with no outstanding side-effects).
  // The field must be present (has_value) to indicate "we have checked
  // whether there is a pending action", even when the answer is "none".
  // Architecture §3.8.3 item 5: "pending_action".
  // Architecture §6.10: "recovery must prioritize reading Checkpoint
  // and pending_action."
  std::optional<std::string> pending_action;

  // -----------------------------------------------------------------------
  // Optional fields (WP03-T012, 6 items)
  // -----------------------------------------------------------------------

  // Correlation back to the originating AgentRequest.request_id.
  // Ensures the checkpoint is traceable to the original request context
  // and supports multi-session auditing. If present, must be non-empty.
  std::optional<std::string> request_id;

  // Correlation to the current GoalContract.goal_id. In multi-goal
  // scenarios, identifies which goal this checkpoint pertains to.
  // If present, must be non-empty.
  std::optional<std::string> goal_id;

  // Reference to the BeliefState snapshot captured at checkpoint time.
  // BeliefState is a separate contract object (WP03-T009); Checkpoint
  // holds a reference (identifier), not the full embedded structure.
  // This supports BeliefState recovery without coupling.
  // If present, must be non-empty.
  std::optional<std::string> belief_state_ref;

  // Current retry count at checkpoint time.
  // Architecture §3.8.3 item 4: "retry_counters" — simplified to a
  // single counter.  Optional because first checkpoint (Running state)
  // may have no retry history.  RecoveryManager (ADR-007 §5.2) handles
  // nullopt as zero retries.
  std::optional<std::uint32_t> retry_count;

  // Checkpoint creation timestamp in milliseconds.
  // Used for checkpoint freshness and temporal ordering.
  // If present, must be positive.
  std::optional<std::int64_t> created_at;

  // Retrieval/audit tags. Do not carry execution control signals.
  // Semantically consistent with other contract object tags
  // (AgentRequest, GoalContract, BeliefState, ContextPacket).
  std::optional<std::vector<std::string>> tags;
};

}  // namespace dasall::contracts
