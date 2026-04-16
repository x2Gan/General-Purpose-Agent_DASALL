#include "ToolValidator.h"

#include <cctype>

#include "tool/ToolRequestGuards.h"

namespace dasall::tools::validation {

namespace {

constexpr std::string_view kDryRunTag = "tool.mode.dry_run";
constexpr std::string_view kValidateOnlyTag = "tool.mode.validate_only";
constexpr std::string_view kPreferMcpRouteTag = "tool.route.mcp";

}  // namespace

ToolValidationResult ToolValidator::validate(
    const contracts::ToolRequest& request,
    const contracts::ToolDescriptor& descriptor) const {
  const auto request_guard = contracts::validate_tool_request_field_rules(request);
  if (!request_guard.ok) {
    return invalid("InvalidRequest", std::string(request_guard.reason));
  }

  const auto descriptor_guard = contracts::validate_tool_descriptor_field_rules(descriptor);
  if (!descriptor_guard.ok) {
    return invalid("InvalidDescriptor", std::string(descriptor_guard.reason));
  }

  if (*request.tool_name != *descriptor.tool_name) {
    return invalid("DescriptorMismatch",
                   "descriptor tool_name must match request.tool_name");
  }

  if (!invocation_matches_category(*request.invocation_kind, *descriptor.category)) {
    return invalid("DescriptorMismatch",
                   "request invocation_kind must match descriptor category");
  }

  const auto operation = derive_operation(request);
  if (!operation.has_value()) {
    return invalid("InvalidValidationMode",
                   "request tags contain conflicting validation operation markers");
  }

  const std::string normalized_arguments = normalize_arguments(*request.arguments_payload);
  if (normalized_arguments.empty()) {
    return invalid("InvalidRequest", "normalized_arguments must not be empty");
  }

  contracts::ToolIR tool_ir = inject_defaults(
      request,
      descriptor,
      *operation,
      normalized_arguments);

  const auto tool_ir_guard = contracts::validate_tool_ir_field_rules(tool_ir);
  if (!tool_ir_guard.ok) {
    return invalid("InvalidToolIR", std::string(tool_ir_guard.reason));
  }

  return ToolValidationResult{
      .tool_ir = std::move(tool_ir),
      .diagnostics = std::nullopt,
  };
}

contracts::ToolIR ToolValidator::inject_defaults(
    const contracts::ToolRequest& request,
    const contracts::ToolDescriptor& descriptor,
    contracts::ToolIROperation operation,
    std::string normalized_arguments) const {
  contracts::ToolIR tool_ir;
  tool_ir.request_id = request.request_id;
  tool_ir.tool_call_id = request.tool_call_id;
  tool_ir.tool_name = request.tool_name;
  tool_ir.operation = operation;
  tool_ir.normalized_arguments = std::move(normalized_arguments);
  tool_ir.route = build_route_hint(descriptor, request);
  tool_ir.timeout_ms = request.timeout_ms.has_value() ? request.timeout_ms
                                                      : descriptor.default_timeout_ms;
  tool_ir.idempotency_key = request.idempotency_key;
  tool_ir.goal_id = request.goal_id;
  tool_ir.worker_task_id = request.worker_task_id;
  return tool_ir;
}

std::string ToolValidator::normalize_arguments(std::string_view arguments_payload) const {
  return trim_ascii(arguments_payload);
}

std::optional<contracts::ToolIROperation> ToolValidator::derive_operation(
    const contracts::ToolRequest& request) const {
  const bool dry_run = has_tag(request.tags, kDryRunTag);
  const bool validate_only = has_tag(request.tags, kValidateOnlyTag);
  if (dry_run && validate_only) {
    return std::nullopt;
  }

  if (dry_run) {
    return contracts::ToolIROperation::DryRun;
  }

  if (validate_only) {
    return contracts::ToolIROperation::ValidateOnly;
  }

  return contracts::ToolIROperation::Invoke;
}

ToolValidationResult ToolValidator::invalid(
    std::string error_code,
    std::string message) {
  return ToolValidationResult{
      .tool_ir = std::nullopt,
      .diagnostics = ValidationDiagnostics{
          .error_code = std::move(error_code),
          .message = std::move(message),
      },
  };
}

bool ToolValidator::invocation_matches_category(
    contracts::ToolInvocationKind invocation_kind,
    contracts::ToolCategory category) {
  switch (invocation_kind) {
    case contracts::ToolInvocationKind::InformationQuery:
      return category == contracts::ToolCategory::Information;
    case contracts::ToolInvocationKind::Action:
      return category == contracts::ToolCategory::Action;
    case contracts::ToolInvocationKind::Workflow:
      return category == contracts::ToolCategory::Workflow;
    case contracts::ToolInvocationKind::AgentDelegation:
      return category == contracts::ToolCategory::AgentDelegation;
    case contracts::ToolInvocationKind::Diagnostic:
      return category == contracts::ToolCategory::Diagnostic;
    case contracts::ToolInvocationKind::Unspecified:
      return false;
  }

  return false;
}

bool ToolValidator::has_tag(
    const std::optional<std::vector<std::string>>& tags,
    std::string_view target_tag) {
  if (!tags.has_value()) {
    return false;
  }

  for (const auto& tag : *tags) {
    if (tag == target_tag) {
      return true;
    }
  }

  return false;
}

std::string ToolValidator::trim_ascii(std::string_view value) {
  std::size_t begin = 0U;
  while (begin < value.size() &&
         std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }

  std::size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1U])) != 0) {
    --end;
  }

  return std::string(value.substr(begin, end - begin));
}

contracts::ToolIRRoute ToolValidator::build_route_hint(
    const contracts::ToolDescriptor& descriptor,
    const contracts::ToolRequest& request) {
  if (has_tag(request.tags, kPreferMcpRouteTag)) {
    return contracts::ToolIRRoute::MCPRemote;
  }

  switch (*descriptor.category) {
    case contracts::ToolCategory::Workflow:
    case contracts::ToolCategory::AgentDelegation:
      return contracts::ToolIRRoute::WorkflowEngine;
    case contracts::ToolCategory::Information:
    case contracts::ToolCategory::Action:
    case contracts::ToolCategory::Diagnostic:
      return contracts::ToolIRRoute::LocalTool;
    case contracts::ToolCategory::Unspecified:
      break;
  }

  return contracts::ToolIRRoute::LocalTool;
}

}  // namespace dasall::tools::validation