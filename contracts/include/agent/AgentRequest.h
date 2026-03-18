#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "checkpoint/RuntimeBudget.h"

namespace dasall::contracts {

// RequestChannel normalizes the entry source for AgentRequest as defined
// by WP03-T003. The Unspecified value follows the WP02-T012 enum lifecycle
// rule: all enums must include an explicit unknown/unspecified sentinel.
enum class RequestChannel {
  Unspecified = 0,
  Cli = 1,
  Gateway = 2,
  Daemon = 3,
  Simulator = 4,
};

// AgentRequest is the unified entry contract object for a single Agent
// request lifecycle, as defined by WP03-T002 semantic boundary and WP03-T003
// field table.
//
// Semantic boundary (WP03-T002 frozen):
//   Allowed:
//     1. Entry intent: user_input, goal_hint, domain_context, request_channel
//     2. Constraints: constraint_set, approval_policy_hint, priority
//     3. Budget: runtime_budget, timeout_ms, deadline_at
//     4. Metadata: request_id, session_id, trace_id, created_at,
//                  idempotency_key, locale, client_capabilities, tags
//
//   Forbidden (WP03-T002 frozen):
//     - Runtime internal state (FSM, retry counters, checkpoint refs)
//     - Provider private fields (rendered_prompt, vendor params)
//     - Execution result state (Observation, BeliefState, etc.)
//     - Multi-agent sub-domain state (WorkerTask, MultiAgentRequest)
//
// Required fields use std::optional to enable guard validation with precise
// diagnostic messages, following the same pattern as ErrorInfo and
// IdentityMetadata contracts.
struct AgentRequest {
  // -----------------------------------------------------------------------
  // Required fields (WP03-T003, 6 items)
  // -----------------------------------------------------------------------

  // Request-level unique identifier, generated at entry and propagated
  // throughout the entire chain. References WP02-T009.
  std::optional<std::string> request_id;

  // Session-level correlation identifier for multi-turn semantic continuity.
  // References WP02-T009.
  std::optional<std::string> session_id;

  // Trace chain identifier for log/event/audit alignment.
  // References WP02-T009.
  std::optional<std::string> trace_id;

  // User's raw intent payload for this turn (text or structured input).
  std::optional<std::string> user_input;

  // Normalized entry source channel. References WP03-T003.
  std::optional<RequestChannel> request_channel;

  // Request creation timestamp in milliseconds, serving as the temporal
  // baseline. References WP02-T010.
  std::optional<std::int64_t> created_at;

  // -----------------------------------------------------------------------
  // Optional fields (WP03-T003, 11 items)
  // -----------------------------------------------------------------------

  // Structured goal hint for downstream GoalContract convergence.
  // Does not replace GoalContract.
  std::optional<std::string> goal_hint;

  // Minimal business context summary carried from entry side.
  // Contains only summary, no rendered messages.
  std::optional<std::string> domain_context;

  // Declarative constraint set (security/policy/permission).
  // Does not contain execution-state fields.
  std::optional<std::string> constraint_set;

  // Entry hint for actions requiring approval.
  // Final policy is determined by downstream objects.
  std::optional<std::string> approval_policy_hint;

  // Budget upper bound declaration (token/turn/tool_call/latency/replan).
  // Dimensions follow WP02-T007.
  std::optional<RuntimeBudget> runtime_budget;

  // Execution duration policy input in milliseconds.
  // If both timeout_ms and deadline_at coexist, deadline_at takes priority.
  std::optional<std::uint32_t> timeout_ms;

  // Hard execution deadline timestamp in milliseconds.
  // Authoritative execution deadline field, references WP02-T010.
  std::optional<std::int64_t> deadline_at;

  // Request priority hint. Advisory only, not directly equal to scheduling
  // state.
  std::optional<std::uint32_t> priority;

  // Idempotency deduplication key. Not equivalent to request_id.
  std::optional<std::string> idempotency_key;

  // Language/region preference. Does not contain provider language parameters.
  std::optional<std::string> locale;

  // Client-visible capability declaration (e.g., streaming support).
  // Capability declaration only, no provider private parameters.
  std::optional<std::string> client_capabilities;

  // Retrieval/audit tags. Do not carry execution control signals.
  std::optional<std::vector<std::string>> tags;
};

}  // namespace dasall::contracts
