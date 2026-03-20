#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "boundary/GuardCommon.h"

namespace dasall::contracts {

enum class ToolIROperation {
  Unspecified = 0,
  Invoke = 1,
  DryRun = 2,
  ValidateOnly = 3,
};

enum class ToolIRPriority {
  Unspecified = 0,
  Low = 1,
  Normal = 2,
  High = 3,
  Urgent = 4,
};

enum class ToolIRRoute {
  Unspecified = 0,
  LocalTool = 1,
  WorkflowEngine = 2,
  MCPRemote = 3,
};

enum class ToolIRBoundaryDecision {
  AllowField,
  RejectDescriptorCatalogField,
  RejectExecutionResultField,
  RejectPromptProviderField,
};

struct ToolIRFieldBoundaryResult {
  bool allowed = true;
  ToolIRBoundaryDecision decision = ToolIRBoundaryDecision::AllowField;
  std::string_view reason = "tool IR field is allowed by WP05-T004";
};

struct ToolIRGuardResult {
  bool ok = false;
  std::string_view reason = "tool IR validation failed";
};

struct ToolIR {
  // Correlation back to the originating AgentRequest.
  std::optional<std::string> request_id;

  // Stable identity for one concrete invocation attempt.
  std::optional<std::string> tool_call_id;

  // Stable registered tool name selected by route and policy gates.
  std::optional<std::string> tool_name;

  // Normalized execution mode after model/protocol intent mapping.
  std::optional<ToolIROperation> operation;

  // Provider-neutral normalized arguments consumed by validators/executors.
  std::optional<std::string> normalized_arguments;

  // Optional route hint selected during capability resolution.
  std::optional<ToolIRRoute> route;

  // Per-call timeout in milliseconds.
  std::optional<std::uint32_t> timeout_ms;

  // Replay-safe anchor for idempotent tool execution.
  std::optional<std::string> idempotency_key;

  // Runtime scheduling priority.
  std::optional<ToolIRPriority> priority;

  // Correlation back to active goal / worker when present.
  std::optional<std::string> goal_id;
  std::optional<std::string> worker_task_id;
};

inline constexpr std::array<std::string_view, 6>
    kToolIRDescriptorCatalogForbiddenFields = {
        "display_name",
        "input_schema_ref",
        "output_schema_ref",
        "required_scopes",
        "version",
        "capability_tier",
};

inline constexpr std::array<std::string_view, 4>
    kToolIRExecutionResultForbiddenFields = {
        "payload",
        "error",
        "side_effects",
        "observation",
};

inline constexpr std::array<std::string_view, 3>
    kToolIRPromptProviderForbiddenFields = {
        "rendered_prompt",
        "provider_payload",
        "final_messages",
};

inline ToolIRGuardResult validate_tool_ir_required_fields(const ToolIR& tool_ir) {
  if (!has_non_empty_value(tool_ir.request_id)) {
    return ToolIRGuardResult{
        .ok = false,
        .reason = "request_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(tool_ir.tool_call_id)) {
    return ToolIRGuardResult{
        .ok = false,
        .reason = "tool_call_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(tool_ir.tool_name)) {
    return ToolIRGuardResult{
        .ok = false,
        .reason = "tool_name is required and must be non-empty",
    };
  }

  if (!tool_ir.operation.has_value() ||
      *tool_ir.operation == ToolIROperation::Unspecified) {
    return ToolIRGuardResult{
        .ok = false,
        .reason = "operation is required and must not be Unspecified",
    };
  }

  if (!has_non_empty_value(tool_ir.normalized_arguments)) {
    return ToolIRGuardResult{
        .ok = false,
        .reason = "normalized_arguments is required and must be non-empty",
    };
  }

  if (!tool_ir.route.has_value() || *tool_ir.route == ToolIRRoute::Unspecified) {
    return ToolIRGuardResult{
        .ok = false,
        .reason = "route is required and must not be Unspecified",
    };
  }

  return ToolIRGuardResult{
      .ok = true,
      .reason = "all required tool IR fields present",
  };
}

inline ToolIRGuardResult validate_tool_ir_field_rules(const ToolIR& tool_ir) {
  const auto required_result = validate_tool_ir_required_fields(tool_ir);
  if (!required_result.ok) {
    return required_result;
  }

  const int raw_operation = static_cast<int>(*tool_ir.operation);
  if (raw_operation < static_cast<int>(ToolIROperation::Invoke) ||
      raw_operation > static_cast<int>(ToolIROperation::ValidateOnly)) {
    return ToolIRGuardResult{
        .ok = false,
        .reason = "operation value is outside the known enum range",
    };
  }

  const int raw_route = static_cast<int>(*tool_ir.route);
  if (raw_route < static_cast<int>(ToolIRRoute::LocalTool) ||
      raw_route > static_cast<int>(ToolIRRoute::MCPRemote)) {
    return ToolIRGuardResult{
        .ok = false,
        .reason = "route value is outside the known enum range",
    };
  }

  if (tool_ir.priority.has_value()) {
    const int raw_priority = static_cast<int>(*tool_ir.priority);
    if (raw_priority < static_cast<int>(ToolIRPriority::Low) ||
        raw_priority > static_cast<int>(ToolIRPriority::Urgent)) {
      return ToolIRGuardResult{
          .ok = false,
          .reason = "priority value is outside the known enum range",
      };
    }
  }

  if (*tool_ir.tool_call_id == *tool_ir.request_id) {
    return ToolIRGuardResult{
        .ok = false,
        .reason = "tool_call_id must not equal request_id because call identity and request identity are layered separately",
    };
  }

  if (tool_ir.timeout_ms.has_value() && *tool_ir.timeout_ms == 0U) {
    return ToolIRGuardResult{
        .ok = false,
        .reason = "timeout_ms must be positive when present",
    };
  }

  if (tool_ir.idempotency_key.has_value() && tool_ir.idempotency_key->empty()) {
    return ToolIRGuardResult{
        .ok = false,
        .reason = "idempotency_key must be non-empty when present",
    };
  }

  if (tool_ir.goal_id.has_value() && tool_ir.goal_id->empty()) {
    return ToolIRGuardResult{
        .ok = false,
        .reason = "goal_id must be non-empty when present",
    };
  }

  if (tool_ir.worker_task_id.has_value() && tool_ir.worker_task_id->empty()) {
    return ToolIRGuardResult{
        .ok = false,
        .reason = "worker_task_id must be non-empty when present",
    };
  }

  return ToolIRGuardResult{
      .ok = true,
      .reason = "tool IR field rules validation passed",
  };
}

constexpr ToolIRFieldBoundaryResult evaluate_tool_ir_field_boundary(
    std::string_view field_name) {
  for (const auto forbidden_field : kToolIRDescriptorCatalogForbiddenFields) {
    if (field_name == forbidden_field) {
      return ToolIRFieldBoundaryResult{
          .allowed = false,
          .decision = ToolIRBoundaryDecision::RejectDescriptorCatalogField,
          .reason = "tool IR must not contain descriptor catalog fields",
      };
    }
  }

  for (const auto forbidden_field : kToolIRExecutionResultForbiddenFields) {
    if (field_name == forbidden_field) {
      return ToolIRFieldBoundaryResult{
          .allowed = false,
          .decision = ToolIRBoundaryDecision::RejectExecutionResultField,
          .reason = "tool IR must not contain execution result fields",
      };
    }
  }

  for (const auto forbidden_field : kToolIRPromptProviderForbiddenFields) {
    if (field_name == forbidden_field) {
      return ToolIRFieldBoundaryResult{
          .allowed = false,
          .decision = ToolIRBoundaryDecision::RejectPromptProviderField,
          .reason = "tool IR must not contain prompt-rendering or provider payload fields",
      };
    }
  }

  return ToolIRFieldBoundaryResult{};
}

inline ToolIRGuardResult validate_tool_ir_forbidden_field(
    std::string_view field_name) {
  const auto result = evaluate_tool_ir_field_boundary(field_name);
  return ToolIRGuardResult{
      .ok = result.allowed,
      .reason = result.reason,
  };
}

}  // namespace dasall::contracts
