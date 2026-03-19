#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "checkpoint/RuntimeBudget.h"

namespace dasall::contracts {

// ToolInvocationKind freezes the stable execution-facing categories that a
// ToolRequest may express. It follows the WP02 enum rule: every enum carries
// an explicit Unspecified sentinel at value 0.
enum class ToolInvocationKind {
  Unspecified = 0,
  InformationQuery = 1,
  Action = 2,
  Workflow = 3,
  AgentDelegation = 4,
  Diagnostic = 5,
};

// ToolRequest is the execution-facing tool invocation contract introduced by
// WP05-T002.
//
// Semantic boundary (WP05-T002 frozen):
//   Allowed:
//     1. Execution identity: request_id, tool_call_id, tool_name
//     2. Invocation intent: invocation_kind, arguments_payload
//     3. Time anchor: created_at
//     4. Correlation anchors: goal_id, worker_task_id
//     5. Reused constraints: runtime_budget, timeout_ms, idempotency_key, tags
//
//   Forbidden:
//     - Result / error / observation fields
//     - Budget snapshot or spent-budget fields
//     - Prompt/provider rendering fields
//     - ToolDescriptor / ToolIR ownership fields
struct ToolRequest {
  // Request-level correlation back to the originating AgentRequest.
  std::optional<std::string> request_id;

  // Stable identity for one concrete tool invocation attempt.
  std::optional<std::string> tool_call_id;

  // Stable registered tool name consumed by ToolRegistry / Executor.
  std::optional<std::string> tool_name;

  // Execution-facing invocation classification.
  std::optional<ToolInvocationKind> invocation_kind;

  // Provider-neutral structured arguments payload for execution.
  std::optional<std::string> arguments_payload;

  // Request creation timestamp in milliseconds.
  std::optional<std::int64_t> created_at;

  // Correlation back to the active goal when a tool call is goal-driven.
  std::optional<std::string> goal_id;

  // Correlation back to a worker task when the invocation is made by a worker.
  std::optional<std::string> worker_task_id;

  // Reuses the shared WP02 runtime budget surface rather than redefining it.
  std::optional<RuntimeBudget> runtime_budget;

  // Per-call timeout in milliseconds.
  std::optional<std::uint32_t> timeout_ms;

  // Replay-safe anchor for idempotent tool execution.
  std::optional<std::string> idempotency_key;

  // Audit and routing tags; they must not carry execution results.
  std::optional<std::vector<std::string>> tags;
};

}  // namespace dasall::contracts