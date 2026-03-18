#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "observation/ObservationSource.h"

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// ObservationDigest is the reasoning-friendly projection of a raw Observation,
// as defined by WP03-T008 semantic boundary.
//
// Core responsibility (architecture 5.2.6):
//   Tool and workflow raw results are suitable for programmatic consumption
//   but not for direct injection into the next reasoning round. The Tool
//   subsystem must therefore provide an Observation Digest that compresses
//   the raw output into a form consumable by ContextOrchestrator and Reasoner.
//
// Five architecture-frozen fields (architecture 5.2.6):
//   1. summary          — Compressed short summary.
//   2. key_facts        — High-value facts retained from the raw output.
//   3. citations        — References to original evidence or results.
//   4. omitted_details  — Hints about what details were trimmed.
//   5. confidence       — Explicit confidence annotation on the digest.
//
// Layering boundary (WP03-T008 frozen, per frozen plan §8.stage2.4):
//   ObservationDigest is strictly separated from Observation:
//     - Digest DOES NOT contain execution-semantic fields:
//         payload, success, error (ErrorInfo), side_effects,
//         tool_call_id, worker_task_id, duration_ms.
//     - Observation DOES NOT contain reasoning-semantic fields:
//         summary, key_facts, citations, omitted_details, confidence.
//
//   Shared fields (with distinct semantics):
//     - observation_id: in Observation it is the primary key; in Digest it
//       is the foreign key linking back to the source Observation.
//     - source: in Observation it is required; in Digest it is optional
//       (redundant copy for consumer convenience).
//     - created_at: in Observation it is the observation creation time;
//       in Digest it is the digest creation time (optional).
//     - tags: optional retrieval/audit tags in both.
//
// Consumers (architecture 5.3.7, ADR-006 §6.1):
//   - ContextOrchestrator: reads latest_observation_digest into ContextPacket.
//   - Memory: persists via memory.write_digest(session_id, digest).
//   - Reasoner (indirect): consumes via ContextPacket.
//   - Planner (indirect): uses digest for replan context.
//
// Forbidden fields (WP03-T008):
//   - Execution result: payload, success, error, side_effects, duration_ms.
//   - Execution correlation: tool_call_id, worker_task_id.
//   - Plan/decision: plan_graph, step_list, action_decision, next_step.
//   - Runtime internal: fsm_state, retry_counters, backoff_ms.
//   - Message rendering: final_messages, rendered_prompt, prompt_bundle.
//   - Provider private: model_provider_args, vendor_tool_schema.
// ---------------------------------------------------------------------------
struct ObservationDigest {
  // -----------------------------------------------------------------------
  // Required fields (WP03-T008, 5 items)
  // -----------------------------------------------------------------------

  // Foreign key linking back to the source Observation that this Digest was
  // derived from. Must match the Observation.observation_id of the source.
  // Used by ContextOrchestrator and Memory for audit trail.
  // References WP02-T009 identification rules.
  std::optional<std::string> observation_id;

  // Compressed short summary of the raw Observation payload.
  // Intended for direct consumption by Reasoner (via ContextPacket) and
  // ContextOrchestrator. Does not contain the full original payload.
  // Architecture 5.2.6 field 1.
  std::optional<std::string> summary;

  // High-value facts extracted from the raw Observation payload.
  // Each fact should be self-contained and independently citable.
  // Reasoner uses these to make next-step decisions without accessing
  // the raw Observation. Architecture 5.2.6 field 2.
  std::optional<std::vector<std::string>> key_facts;

  // References to original evidence or result excerpts.
  // Supports audit trail and evidence traceability. Does not contain
  // the full original text. Architecture 5.2.6 field 3.
  std::optional<std::vector<std::string>> citations;

  // Explicit confidence annotation on this digest, in [0.0, 1.0].
  // 0.0 = completely unreliable, 1.0 = fully reliable.
  // This measures digest quality (summarization fidelity), NOT execution
  // success probability. Architecture 5.2.6 field 5.
  std::optional<float> confidence;

  // -----------------------------------------------------------------------
  // Optional fields (WP03-T008, 4 items)
  // -----------------------------------------------------------------------

  // Hints about which details from the original Observation payload were
  // trimmed and not included in summary or key_facts. Allows consumers to
  // judge completeness and decide whether to fetch the full Observation.
  // Architecture 5.2.6 field 4. May be absent when nothing was trimmed.
  std::optional<std::vector<std::string>> omitted_details;

  // Redundant copy of the source Observation's ObservationSource enum.
  // Allows consumers to classify the digest without fetching the source
  // Observation. If present, must not be Unspecified.
  std::optional<ObservationSource> source;

  // Digest creation timestamp in milliseconds (WP02-T010).
  // Distinct from Observation.created_at (which is observation creation time).
  // If present, must be positive.
  std::optional<std::int64_t> created_at;

  // Retrieval/audit tags. Do not carry execution control signals.
  // Semantically consistent with Observation.tags.
  std::optional<std::vector<std::string>> tags;
};

}  // namespace dasall::contracts
