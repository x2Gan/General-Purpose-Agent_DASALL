#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "error/ErrorInfo.h"

namespace dasall::contracts {

// ToolResult is the execution-output contract introduced by WP05-T003.
//
// Semantic boundary (WP05-T003 frozen):
//   Allowed:
//     1. Execution identity: request_id, tool_call_id, tool_name
//     2. Execution outcome: success, payload, error
//     3. Side-effect declaration: side_effects
//     4. Completion timing: completed_at, duration_ms
//     5. Correlation anchors: goal_id, worker_task_id
//     6. Audit metadata: tags
//
//   Forbidden:
//     - Observation / ObservationDigest ownership fields
//     - Runtime budget snapshot or retry accounting fields
//     - Prompt/provider rendering fields
//     - ToolDescriptor / ToolIR ownership fields
//     - Recovery / compensation control-plan fields
struct ToolResult {
  // Correlation back to the originating AgentRequest.
  std::optional<std::string> request_id;

  // Stable identity for one concrete tool invocation attempt.
  std::optional<std::string> tool_call_id;

  // Stable registered tool name that produced this result.
  std::optional<std::string> tool_name;

  // Whether the tool execution completed successfully.
  std::optional<bool> success;

  // Raw structured tool output for programmatic consumption.
  std::optional<std::string> payload;

  // Structured failure information when success is false.
  std::optional<ErrorInfo> error;

  // Irreversible side effects declared by the tool execution.
  std::optional<std::vector<std::string>> side_effects;

  // Completion timestamp in milliseconds.
  std::optional<std::int64_t> completed_at;

  // Execution duration in milliseconds.
  std::optional<std::int64_t> duration_ms;

  // Correlation back to the active goal when tool execution is goal-driven.
  std::optional<std::string> goal_id;

  // Optional worker-task anchor when the tool executes inside worker scope.
  std::optional<std::string> worker_task_id;

  // Audit and routing tags; they must not carry control-plane signals.
  std::optional<std::vector<std::string>> tags;
};

}  // namespace dasall::contracts