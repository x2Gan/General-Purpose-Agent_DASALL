#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// RecoveryOutcome is the runtime-owned execution-result object defined by
// WP04-T011.
//
// Responsibility:
//   RecoveryOutcome records what RecoveryManager actually decided/executed after
//   evaluating a RecoveryRequest. It is the stable output object for recovery
//   admission and execution, carrying only execution-result semantics and the
//   minimum control metadata needed for audit and continuation.
//
// What this object is allowed to express:
//   - executed_action: the actual recovery action that runtime took.
//   - final_runtime_state: the runtime state after the action/admission result.
//   - updated_retry_count: the post-decision retry-count snapshot.
//   - checkpoint_ref: reference to the checkpoint produced or advanced.
//   - compensation_result_ref: reference to any compensation result record.
//   - rejection_reason / escalation_reason: audit reasons for non-execution or
//     escalation outcomes.
//
// What this object must never express:
//   - failure_root_cause, root_cause_analysis, belief_patch,
//     plan_patch_hint.
//   These belong to ReflectionDecision or other cognition-layer artifacts and
//   are enforced by RecoveryBoundaryGuards / RecoveryOutcomeGuards.
//
// T011 scope discipline:
//   - T011 freezes the object skeleton and required/boundary guards only.
//   - T012 will add field-table hygiene and combination rules.
// ---------------------------------------------------------------------------
struct RecoveryOutcome {
  // -----------------------------------------------------------------------
  // Required fields (T011 semantic minimum)
  // -----------------------------------------------------------------------

  // The actual recovery action executed or selected by RecoveryManager.
  // This is deliberately a runtime result string/category slot rather than a
  // cognition hint; it answers "what did runtime do?".
  std::optional<std::string> executed_action;

  // The runtime state after the recovery decision/action completed.
  // This answers "where did runtime end up after recovery admission/execution?".
  std::optional<std::string> final_runtime_state;

  // -----------------------------------------------------------------------
  // Optional fields (frozen as legal slots by T011; detailed rules deferred
  // to T012 field-table work)
  // -----------------------------------------------------------------------

  // Post-decision retry-count snapshot. This reuses the repository-wide
  // simplification that maps retry_counters to a single uint32 retry_count.
  std::optional<std::uint32_t> updated_retry_count;

  // Reference to the checkpoint that anchors the recovery result. The result
  // object intentionally references the checkpoint instead of embedding the full
  // Checkpoint object again.
  std::optional<std::string> checkpoint_ref;

  // Reference to any compensation artifact/result produced during recovery.
  std::optional<std::string> compensation_result_ref;

  // Audit reason for an admission rejection or non-executed outcome.
  std::optional<std::string> rejection_reason;

  // Audit reason for escalation/manual intervention outcomes.
  std::optional<std::string> escalation_reason;
};

}  // namespace dasall::contracts