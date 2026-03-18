#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// ReflectionDecisionKind enumerates the suggestion-level outcomes that
// ReflectionEngine can emit after analyzing a failure.
//
// ADR-007 freezes ReflectionDecision as a cognition-owned suggestion object,
// not a runtime control object. The enum therefore captures only semantic
// next-step suggestions and intentionally excludes any scheduling concepts.
//
// Enum range: [0, 4]. Guards reject Unspecified and out-of-range values.
// ---------------------------------------------------------------------------
enum class ReflectionDecisionKind : int {
  Unspecified = 0,
  Continue = 1,
  RetryStep = 2,
  Replan = 3,
  AbortSafe = 4,
};

// ---------------------------------------------------------------------------
// ReflectionDecision is the cognition-layer failure-semantics suggestion
// object defined by WP04-T007.
//
// Responsibility:
//   ReflectionDecision records how ReflectionEngine interprets the current
//   failure and what semantic next-step it recommends. It is consumed by
//   RecoveryManager, which combines this suggestion with runtime constraints
//   such as idempotency, budget, and circuit-breaker state before any real
//   recovery action is executed.
//
// What this object is allowed to express:
//   - decision_kind: semantic next-step category.
//   - rationale: explanation of the failure interpretation.
//   - confidence: optional trust score for the suggestion.
//   - relevant_observation_refs: evidence references backing the suggestion.
//   - hint_ref: optional reference to a cognition-side hint artifact.
//
// What this object must never express:
//   - retry_after_ms, backoff_strategy, lease_extension,
//     checkpoint_blob, circuit_breaker_transition.
//   These remain runtime concerns and are enforced by
//   RecoveryBoundaryGuards / ReflectionDecisionGuards.
//
// T007 scope discipline:
//   - Required-field and boundary validation live in T007-B.
//   - Detailed optional-field hygiene and field-table rules live in T008.
// ---------------------------------------------------------------------------
struct ReflectionDecision {
  // -----------------------------------------------------------------------
  // Required fields (T007 semantic minimum)
  // -----------------------------------------------------------------------

  // Correlates the decision back to the originating AgentRequest chain.
  // This keeps the cognition suggestion auditable and aligned with the
  // cross-cutting identification rules from WP02-T009.
  std::optional<std::string> request_id;

  // Semantic next-step suggestion chosen by ReflectionEngine.
  // Must be present and must not be Unspecified.
  std::optional<ReflectionDecisionKind> decision_kind;

  // Human-readable explanation of why ReflectionEngine produced the current
  // suggestion. This is the minimum reasoning payload required to keep the
  // contract from collapsing into an opaque boolean flag.
  std::optional<std::string> rationale;

  // -----------------------------------------------------------------------
  // Optional fields (frozen as legal slots by T007; detailed rules deferred
  // to T008 field-table work)
  // -----------------------------------------------------------------------

  // Optional goal correlation for multi-goal scenarios.
  std::optional<std::string> goal_id;

  // Optional confidence score for the suggestion, expected to stay within
  // the closed interval [0.0, 1.0].
  std::optional<float> confidence;

  // Observation identifiers that support the suggestion. Observation.h
  // explicitly lists these refs as valid consumers of Observation identity.
  std::optional<std::vector<std::string>> relevant_observation_refs;

  // Generic reference to a cognition-side hint artifact. T007 keeps this as a
  // single reference slot instead of expanding multiple hint payload fields.
  std::optional<std::string> hint_ref;

  // Optional creation timestamp in milliseconds for audit and ordering.
  std::optional<std::int64_t> created_at;

  // Optional retrieval and audit tags. These remain passive metadata and must
  // not encode runtime control signals.
  std::optional<std::vector<std::string>> tags;
};

}  // namespace dasall::contracts