#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "checkpoint/BudgetSnapshot.h"
#include "checkpoint/Checkpoint.h"
#include "checkpoint/ReflectionDecision.h"
#include "error/ErrorInfo.h"
#include "observation/Observation.h"

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// IdempotencyAndSideEffectReport is the minimum admission-evidence payload that
// RecoveryManager consumes before deciding whether a failed step can be safely
// replayed.
//
// Responsibility:
//   - Express whether the failed step is safe to replay.
//   - Carry the idempotency key used for deduplication, when available.
//   - State whether side-effects have already been observed.
//   - Explain why replay is unsafe when replay_safe=false.
//
// What this report is NOT:
//   - NOT the raw execution record (Observation already owns payload/error/
//     side_effects).
//   - NOT a compensation plan or retry schedule.
//   - NOT a policy decision result; it is admission evidence only.
// ---------------------------------------------------------------------------
struct IdempotencyAndSideEffectReport {
  // Whether the failed operation can be replayed without violating
  // idempotency or duplicating irreversible side-effects.
  std::optional<bool> replay_safe;

  // Idempotency key used by the upstream execution path, when available.
  std::optional<std::string> idempotency_key;

  // Whether side-effects were already observed for the failing step.
  std::optional<bool> side_effects_present;

  // Explanation for non-replayable situations. Must only be populated when
  // replay_safe=false.
  std::optional<std::string> non_replayable_reason;
};

// ---------------------------------------------------------------------------
// RecoveryRequest is the runtime-owned admission input object defined by
// WP04-T009.
//
// Responsibility:
//   RecoveryRequest binds the cognition-owned ReflectionDecision to the runtime
//   evidence that RecoveryManager must inspect before it can allow any real
//   recovery action. It is the contract object that answers the question:
//   "Given this failure, this suggestion, this checkpoint, this retry count,
//    this budget snapshot, and this replay evidence, may runtime proceed?"
//
// What this object is allowed to express:
//   - The nested ReflectionDecision suggestion.
//   - Structured failure evidence (ErrorInfo + Observation).
//   - The recovery anchor state (Checkpoint).
//   - Retry-count and budget-snapshot evidence.
//   - Idempotency and side-effect admission evidence.
//
// What this object must never express at the top level:
//   - Reflection semantics again (decision_kind, rationale, confidence, hints).
//   - RecoveryOutcome fields (executed_action, final_runtime_state,
//     rejection_reason, checkpoint_ref, compensation_result_ref).
//   - Execution scheduling outputs (retry_after_ms, backoff_strategy,
//     circuit_breaker_transition).
//
// T009 scope discipline:
//   - T009 freezes the object skeleton and required/boundary guards only.
//   - T010 will add detailed field-table hygiene and combination rules.
// ---------------------------------------------------------------------------
struct RecoveryRequest {
  // -----------------------------------------------------------------------
  // Required fields (semantic minimum for runtime admission)
  // -----------------------------------------------------------------------

  // Cognition-owned suggestion emitted by ReflectionEngine. RecoveryRequest
  // consumes this object; it must not duplicate its top-level semantics.
  std::optional<ReflectionDecision> reflection_decision;

  // Structured failure information used for retry/replan/admission decisions.
  std::optional<ErrorInfo> error_info;

  // The latest failed execution observation that triggered recovery.
  std::optional<Observation> latest_observation;

  // Recovery anchor snapshot persisted by runtime before/after state changes.
  std::optional<Checkpoint> checkpoint;

  // Runtime admission evidence describing replay safety and side-effects.
  std::optional<IdempotencyAndSideEffectReport>
      idempotency_and_side_effect_report;

  // -----------------------------------------------------------------------
  // Optional fields (frozen as legal slots by T009; detailed rules deferred
  // to T010 field-table work)
  // -----------------------------------------------------------------------

  // Current retry-counter snapshot. This reuses the repository-wide
  // simplification that maps retry_counters to a single uint32 retry_count.
  std::optional<std::uint32_t> retry_count;

  // Snapshot of runtime budget usage at admission time. Reuses the WP02
  // frozen BudgetSnapshot object rather than inventing new budget fields.
  std::optional<BudgetSnapshot> runtime_budget_snapshot;
};

}  // namespace dasall::contracts