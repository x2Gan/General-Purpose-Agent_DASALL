#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "context/InputSafetySignal.h"
#include "context/RetrievalEvidenceRef.h"

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// ContextPacket is the structured semantic context object produced by
// ContextOrchestrator and consumed by Cognition layer and PromptComposer,
// as defined by WP03-T010 semantic composition.
//
// Main-flow chain position (WP03-T001):
//   AgentRequest -> GoalContract -> [ContextPacket] -> Observation
//     -> ObservationDigest -> BeliefState -> Checkpoint -> AgentResult
//
// Core responsibility (architecture 3.8.5, ADR-006 §3.2):
//   ContextPacket is the output of ContextOrchestrator (memory subsystem).
//   It aggregates all semantic information needed for the current reasoning
//   round — user input, goal summary, conversation history, memory,
//   retrieved evidence, observation digests, tool visibility, governance
//   policy, token budget report, and belief state summary — into a single
//   structured object that both Cognition components and PromptComposer
//   can consume without reaching back into memory/knowledge internals.
//
// ADR-006 §6.1 semantic slots (10 categories, all covered):
//   1. user_turn                          (Required)
//   2. current_goal / goal_contract       (Required: current_goal_summary)
//   3. recent_history                     (Required)
//   4. summary_memory                     (Optional)
//   5. retrieval_evidence                 (Optional; textual view)
//   6. latest_observation_digest          (Optional)
//   7. active_tools / visible_capabilities(Optional)
//   8. policy_digest                      (Optional)
//   9. token_budget_report                (Optional)
//  10. belief_state / fact view           (Optional)
//
// INT-TODO-008 adds RetrievalEvidenceRef[] as a supporting contract for slot 5
// without changing the original 10-slot semantic taxonomy.
// COG-GAP-016 adds InputSafetySignal as an additive entry-safety signal for
// slot 1/slot 8 consumers without promoting protocol-private access metadata
// into ContextPacket.
//
// What ContextPacket is NOT (WP03-T010 frozen, ADR-006 §6.1/§8):
//   - NOT a prompt message: final_messages, rendered_prompt, provider_payload
//     belong to PromptComposeResult (ADR-006 §6.1 prohibition).
//   - NOT an execution record: payload, success, error, side_effects
//     belong to Observation (WP03-T006).
//   - NOT a cognitive state snapshot: confirmed_facts, hypotheses,
//     assumptions belong to BeliefState (WP03-T009).
//   - NOT a recovery checkpoint: working_memory_snapshot, pending_action
//     belong to Checkpoint.
//   - NOT a user entry point: attachments, request_channel belong to
//     AgentRequest (WP03-T002).
//   - NOT a plan/decision: plan_graph, step_list, action_decision belong
//     to Planner/Reasoner (architecture 4.3).
//   - NOT a model adapter config: model_provider_args, vendor_tool_schema
//     belong to LLMAdapter (ADR-006 §3.2).
//
// Consumers (architecture 4.3, 5.8.3, ADR-006 §4):
//   - Planner: reads goal_summary + recent_history + evidence for planning.
//   - Reasoner: reads full semantic context for next-step decisions.
//   - ReflectionEngine: reads ContextPacket for failure attribution.
//   - ResponseBuilder: reads ContextPacket + execution results for output.
//   - PerceptionEngine: reads user_turn + context for intent extraction.
//   - PromptComposer: maps ContextPacket slots to Prompt template variables.
//   - PromptPolicy: applies redaction/filtering on ContextPacket sources.
//   - Checkpoint: snapshots ContextPacket for recovery.
//
// Producer (ADR-006 §3.2):
//   - ContextOrchestrator (memory subsystem): sole producer.
//
// Forbidden fields (ADR-006 §6.1/§8 + WP03 frozen boundaries):
//   - Message rendering: final_messages, rendered_prompt, provider_payload,
//     prompt_bundle, system_instructions, prompt_template, few_shots,
//     output_schema.
//   - Model vendor: model_provider_args, vendor_tool_schema.
//   - Execution results: payload, success, error, side_effects,
//     tool_call_id, worker_task_id, duration_ms.
//   - Runtime internal: fsm_state, retry_counters, backoff_ms.
//   - Recovery: working_memory_snapshot, pending_action.
//   - Plan/decision: plan_graph, step_list, action_decision, next_step.
//   - Multi-Agent: worker_task_list, lease_id, delegation_policy.
//   - Entry request: attachments, request_channel.
// ---------------------------------------------------------------------------
struct ContextPacket {
  // -----------------------------------------------------------------------
  // Required fields (WP03-T010, 4 items)
  // -----------------------------------------------------------------------

  // Correlation back to the originating AgentRequest.request_id.
  // Ensures the context packet is traceable to the original request
  // and supports multi-session auditing. References WP02-T009.
  std::optional<std::string> request_id;

  // Current user input or turn text. Even system-triggered turns must
  // provide a representative text describing the trigger.
  // Architecture 3.8.5 item 1, ADR-006 §6.1 item 1.
  std::optional<std::string> user_turn;

  // GoalContract summary text — a 1-2 sentence compression of the
  // current GoalContract, NOT the full GoalContract struct.
  // Consumed by Planner, Reasoner, and PromptComposer for goal-aware
  // reasoning without coupling to the full GoalContract schema.
  // ADR-006 §6.1 item 2.
  std::optional<std::string> current_goal_summary;

  // Recent conversation/action history entries. First turn uses an
  // empty vector (has_value() == true, size() == 0).
  // Ordered chronologically, most recent last.
  // Architecture 3.8.5 item 3 ("recent_history"), ADR-006 §6.1 item 3.
  std::optional<std::vector<std::string>> recent_history;

  // -----------------------------------------------------------------------
  // Optional fields (WP03-T010 base 9 items + INT-TODO-008 supporting ref view)
  // -----------------------------------------------------------------------

  // Condensed long-term / summary memory content. Absent when no
  // summary has been produced yet (e.g. first session turn).
  // Architecture 3.8.5 item 4, ADR-006 §6.1 item 4.
  std::optional<std::string> summary_memory;

  // Retrieved knowledge or evidence entries from Knowledge Service,
  // Long-Term Memory, or Vector Memory. Absent when no retrieval
  // was performed in the current round.
  // Architecture 3.8.5 item 5, ADR-006 §6.1 item 5.
  std::optional<std::vector<std::string>> retrieval_evidence;

  // Additive + optional structured provenance view for retrieval_evidence.
  // Preserves evidence_ref/source_ref/source_kind/summary_text/trust_level/
  // freshness/(optional) anchor_locator without lifting full EvidenceSlice or
  // EvidenceBundle into shared contracts.
  std::optional<std::vector<RetrievalEvidenceRef>> retrieval_evidence_refs;

  // Most recent ObservationDigest summary text. Absent on the first
  // turn when no observation has been collected. This is a text
  // summary, NOT the full ObservationDigest struct.
  // ADR-006 §6.1 item 6.
  std::optional<std::string> latest_observation_digest_summary;

  // Visible tool or capability identifiers that ContextOrchestrator
  // determines are relevant to the current round. Absent if no tools
  // are available or tool context is not needed.
  // Architecture 3.8.5 item 7, ADR-006 §6.1 item 7.
  std::optional<std::vector<std::string>> active_tools;

  // Governance policy summary text. Summarizes security boundaries,
  // rate limits, confirmation requirements, or content restrictions
  // that apply to the current context. Absent when no policy
  // constraints are active.
  // Architecture 3.8.5 item 8, ADR-006 §6.1 item 8.
  std::optional<std::string> policy_digest;

  // Additive + optional entry-safety scan signal derived from user_turn.
  // Carries only normalized prompt-injection / PII detection facts and
  // low-cardinality reason codes; does not expose protocol headers or
  // provider-private safety payloads.
  std::optional<InputSafetySignal> input_safety_signal;

  // Token budget allocation report. Records how ContextOrchestrator
  // distributed the available token budget across semantic slots,
  // including any dropped_items or compression_notes. Absent when
  // budget tracking is not enabled.
  // Architecture 3.8.5 item 9, ADR-006 §6.1 item 9.
  std::optional<std::string> token_budget_report;

  // BeliefState or equivalent fact view summary text.
  // A textual projection of the current BeliefState for context
  // consumers that don't need the full BeliefState struct.
  // Absent when no belief state has been formed yet.
  // ADR-006 §6.1 item 10.
  std::optional<std::string> belief_state_summary;

  // ContextPacket assembly completion timestamp in milliseconds
  // (WP02-T010). Used for freshness judgment and temporal ordering.
  // If present, must be positive.
  std::optional<std::int64_t> created_at;

  // Retrieval/audit tags. Do not carry execution control signals.
  // Semantically consistent with other contract object tags
  // (AgentRequest, GoalContract, BeliefState, etc.).
  std::optional<std::vector<std::string>> tags;
};

}  // namespace dasall::contracts
