#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// BeliefState is the structured cognitive state of an Agent within a single
// request lifecycle, as defined by WP03-T009 semantic boundary.
//
// Main-flow chain position (WP03-T001):
//   AgentRequest -> GoalContract -> ContextPacket -> Observation
//     -> ObservationDigest -> [BeliefState] -> Checkpoint -> AgentResult
//
// Core responsibility (architecture 3.8.2):
//   BeliefState must explicitly distinguish confirmed_facts, hypotheses,
//   assumptions, evidence_refs, and confidence.  It synthesizes observation
//   digests, prior knowledge, and reasoning conclusions into a structured
//   form that Planner, Reasoner, ReflectionEngine, and ContextOrchestrator
//   can consume — avoiding the situation where the system treats transient
//   speculation as stable fact.
//
// Five architecture-frozen fields (architecture 3.8.2):
//   1. confirmed_facts  — Verified true statements.
//   2. hypotheses       — Tentative beliefs pending confirmation.
//   3. assumptions      — Taken-as-true premises, not yet verified.
//   4. evidence_refs    — Pointers to supporting evidence.
//   5. confidence       — Overall belief state reliability [0.0, 1.0].
//
// What BeliefState is NOT (WP03-T009 frozen):
//   - NOT an entry point: user request semantics belong to AgentRequest.
//   - NOT a recovery snapshot: runtime state belongs to Checkpoint.
//   - NOT an execution record: raw results belong to Observation.
//   - NOT a reasoning summary: compressed digests belong to ObservationDigest.
//
// Consumers (architecture 4.3, 6.2, ADR-006 §6.1, ADR-007 §3.2):
//   - Planner: reads confirmed_facts + hypotheses for plan generation.
//   - Reasoner: reads BeliefState for next-step decisions.
//   - ReflectionEngine: reads BeliefState for failure attribution (ADR-007).
//   - ContextOrchestrator: includes belief_state in ContextPacket (ADR-006).
//   - Checkpoint: snapshots BeliefState for recovery.
//
// Forbidden fields (WP03-T009):
//   - Observation execution: payload, success, error, side_effects,
//     tool_call_id, worker_task_id, duration_ms.
//   - ObservationDigest reasoning: summary, key_facts, citations,
//     omitted_details.
//   - Plan/decision: plan_graph, step_list, action_decision, next_step.
//   - Runtime internal: fsm_state, retry_counters, backoff_ms.
//   - Message rendering: final_messages, rendered_prompt, prompt_bundle.
//   - Provider private: model_provider_args, vendor_tool_schema.
//   - Checkpoint recovery: working_memory_snapshot, pending_action.
//   - Entry request: user_input, request_channel, attachments.
//   - Multi-Agent: worker_task_list, lease_id, delegation_policy.
// ---------------------------------------------------------------------------
struct BeliefState {
  // -----------------------------------------------------------------------
  // Required fields (WP03-T009, 6 items)
  // -----------------------------------------------------------------------

  // Correlation back to the originating AgentRequest.request_id.
  // Ensures the belief state is traceable to the original request context
  // and supports multi-session auditing. References WP02-T009.
  std::optional<std::string> request_id;

  // Verified true statements. Derived from Observation/ObservationDigest
  // validation, human confirmation, or reasoning engine conclusions.
  // Each fact should be self-contained and independently evaluable.
  // Empty vector is valid (initial state with no confirmed facts).
  // Architecture 3.8.2 field 1.
  std::optional<std::vector<std::string>> confirmed_facts;

  // Tentative beliefs pending confirmation. Supported by evidence but
  // not yet verified. Planner and Reasoner use these for conditional
  // decision making. Architecture 3.8.2 field 2.
  std::optional<std::vector<std::string>> hypotheses;

  // Taken-as-true premises that have not been independently verified.
  // If an assumption is later disproven, the decision chain depending
  // on it needs re-evaluation by ReflectionEngine.
  // Architecture 3.8.2 field 3.
  std::optional<std::vector<std::string>> assumptions;

  // References to evidence supporting the current belief state.
  // Points to Observation IDs, ObservationDigest IDs, or knowledge
  // entry identifiers. Supports audit trail and traceability.
  // Architecture 3.8.2 field 4.
  std::optional<std::vector<std::string>> evidence_refs;

  // Overall belief state reliability, in [0.0, 1.0].
  // 0.0 = completely unreliable belief, 1.0 = fully reliable belief.
  // Reflects overall cognitive certainty, NOT the confidence of any
  // single fact. Distinct from ObservationDigest.confidence which
  // measures summarization fidelity.
  // Architecture 3.8.2 field 5.
  std::optional<float> confidence;

  // -----------------------------------------------------------------------
  // Optional fields (WP03-T009, 3 items)
  // -----------------------------------------------------------------------

  // Correlation to the current GoalContract.goal_id. In multi-goal
  // scenarios, identifies which goal this BeliefState pertains to.
  // Absent (nullopt) in single-goal scenarios where request_id
  // provides sufficient correlation. If present, must be non-empty.
  std::optional<std::string> goal_id;

  // Belief state formation timestamp in milliseconds (WP02-T010).
  // Used to judge belief freshness and temporal ordering.
  // If present, must be positive.
  std::optional<std::int64_t> created_at;

  // Retrieval/audit tags. Do not carry execution control signals.
  // Semantically consistent with other contract object tags.
  std::optional<std::vector<std::string>> tags;
};

}  // namespace dasall::contracts
