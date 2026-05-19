#include "bridge/ToolServiceBridge.h"

#include <stdexcept>
#include <utility>

#include "RuntimePolicySnapshot.h"
#include "ToolManager.h"

namespace {

constexpr std::uint64_t kMinimumDeadlineMs = 1U;

[[nodiscard]] std::string copy_or_default(const std::optional<std::string>& value,
                                          std::string fallback) {
  if (value.has_value() && !value->empty()) {
    return *value;
  }

  return fallback;
}

[[nodiscard]] std::string normalize_capability_id(std::string_view value) {
  return std::string(value);
}

[[nodiscard]] std::string default_target_id_for(std::string_view capability_id) {
  return std::string("builtin:") + std::string(capability_id);
}

}  // namespace

namespace dasall::tools::bridge {

std::optional<CompensationTargetRef> resolve_compensation_target(
    std::string_view target_ref) {
  if (target_ref.empty()) {
    return std::nullopt;
  }

  constexpr std::string_view kToolScheme = "tool://";
  if (target_ref.substr(0, kToolScheme.size()) == kToolScheme) {
    const auto remainder = target_ref.substr(kToolScheme.size());
    const auto separator = remainder.find('/');
    const auto capability_id = normalize_capability_id(
        separator == std::string_view::npos ? remainder : remainder.substr(0, separator));
    if (capability_id.empty()) {
      return std::nullopt;
    }

    auto target_id = separator == std::string_view::npos
                         ? default_target_id_for(capability_id)
                         : std::string(remainder.substr(separator + 1U));
    if (target_id.empty()) {
      target_id = default_target_id_for(capability_id);
    }

    return CompensationTargetRef{
        .capability_id = std::move(capability_id),
        .target_id = std::move(target_id),
    };
  }

  return CompensationTargetRef{
      .capability_id = normalize_capability_id(target_ref),
      .target_id = default_target_id_for(target_ref),
  };
}

std::string format_compensation_target_ref(
    std::string_view capability_id,
    std::string_view target_id) {
  if (capability_id.empty()) {
    return std::string();
  }

  std::string formatted = std::string("tool://") + std::string(capability_id);
  if (!target_id.empty()) {
    formatted.push_back('/');
    formatted += target_id;
  }

  return formatted;
}

services::ServiceCallContext ToolServiceBridge::build_context(
    const contracts::ToolIR& tool_ir,
    const ToolInvocationContext& invocation_context) const {
  const auto request_id = resolve_request_id(tool_ir);
  const auto tool_call_id = resolve_tool_call_id(tool_ir, request_id);

  return services::ServiceCallContext{
      .request_id = request_id,
      .session_id = resolve_session_id(invocation_context, request_id),
      .trace_id = resolve_trace_id(invocation_context, tool_call_id),
      .tool_call_id = tool_call_id,
      .goal_id = resolve_goal_id(tool_ir, tool_call_id),
      .budget_guard = resolve_budget_guard(invocation_context),
      .deadline_ms = resolve_deadline_ms(tool_ir, invocation_context),
  };
}

services::ExecutionCompensationRequest ToolServiceBridge::build_compensation_request(
    const contracts::ToolIR& tool_ir,
    const CompensationRequest& request,
    const ToolInvocationContext& invocation_context) const {
  const auto target = resolve_compensation_target(request.target_ref.value_or(std::string{}));
  if (!target.has_value()) {
    throw std::invalid_argument("builtin.executor.compensation_target_invalid");
  }

  return services::ExecutionCompensationRequest{
      .context = build_context(tool_ir, invocation_context),
      .target = services::CapabilityTargetRef{
          .capability_id = target->capability_id,
          .target_id = target->target_id,
      },
      .compensation_action = request.compensation_action.value_or(resolve_tool_name(tool_ir)),
      .arguments_json = std::string("{}"),
      .source_execution_id = request.tool_call_id.value_or(
          tool_ir.tool_call_id.value_or(std::string("unknown_call"))),
      .reason_code = request.reason_code.value_or(std::string("tool.manager.compensation_requested")),
  };
}

services::ExecutionCommandRequest ToolServiceBridge::build_action_request(
    const contracts::ToolIR& tool_ir,
    const ToolInvocationContext& invocation_context) const {
  return services::ExecutionCommandRequest{
      .context = build_context(tool_ir, invocation_context),
      .target = build_target(tool_ir),
      .action = resolve_tool_name(tool_ir),
      .arguments_json = tool_ir.normalized_arguments.value_or(std::string("{}")),
      .idempotency_key = tool_ir.idempotency_key,
  };
}

services::DataQueryRequest ToolServiceBridge::build_query_request(
    const contracts::ToolIR& tool_ir,
    const ToolInvocationContext& invocation_context) const {
  return services::DataQueryRequest{
      .context = build_context(tool_ir, invocation_context),
      .dataset = resolve_tool_name(tool_ir),
      .filters_json = tool_ir.normalized_arguments.value_or(std::string("{}")),
      .projection = std::string("default"),
      .freshness = resolve_query_freshness(invocation_context),
  };
}

services::ExecutionDiagnoseRequest ToolServiceBridge::build_diagnose_request(
    const contracts::ToolIR& tool_ir,
    const ToolInvocationContext& invocation_context) const {
  return services::ExecutionDiagnoseRequest{
      .context = build_context(tool_ir, invocation_context),
      .target = build_target(tool_ir),
      .include_last_error = true,
  };
}

std::string ToolServiceBridge::resolve_request_id(const contracts::ToolIR& tool_ir) {
  return copy_or_default(tool_ir.request_id, std::string("tool.request"));
}

std::string ToolServiceBridge::resolve_tool_call_id(const contracts::ToolIR& tool_ir,
                                                    std::string_view request_id) {
  return copy_or_default(tool_ir.tool_call_id, std::string(request_id) + ".tool");
}

std::string ToolServiceBridge::resolve_tool_name(const contracts::ToolIR& tool_ir) {
  return copy_or_default(tool_ir.tool_name, std::string("unknown_tool"));
}

std::string ToolServiceBridge::resolve_session_id(
    const ToolInvocationContext& invocation_context,
    std::string_view request_id) {
  return copy_or_default(invocation_context.session_id,
                         std::string(request_id) + ".session");
}

std::string ToolServiceBridge::resolve_trace_id(
    const ToolInvocationContext& invocation_context,
    std::string_view tool_call_id) {
  return copy_or_default(invocation_context.trace.trace_id,
                         std::string(tool_call_id) + ".trace");
}

std::string ToolServiceBridge::resolve_goal_id(const contracts::ToolIR& tool_ir,
                                               std::string_view tool_call_id) {
  return copy_or_default(tool_ir.goal_id, std::string(tool_call_id) + ".goal");
}

std::uint64_t ToolServiceBridge::resolve_deadline_ms(
    const contracts::ToolIR& tool_ir,
    const ToolInvocationContext& invocation_context) {
  if (tool_ir.timeout_ms.has_value() && *tool_ir.timeout_ms > 0U) {
    return *tool_ir.timeout_ms;
  }

  if (invocation_context.profile_snapshot != nullptr) {
    const auto configured_timeout_ms = invocation_context.profile_snapshot->timeout_policy().tool.timeout_ms;
    if (configured_timeout_ms > 0) {
      return static_cast<std::uint64_t>(configured_timeout_ms);
    }
  }

  return kMinimumDeadlineMs;
}

std::optional<contracts::RuntimeBudget> ToolServiceBridge::resolve_budget_guard(
    const ToolInvocationContext& invocation_context) {
  if (invocation_context.profile_snapshot == nullptr) {
    return std::nullopt;
  }

  return invocation_context.profile_snapshot->runtime_budget();
}

services::ServiceDataFreshness ToolServiceBridge::resolve_query_freshness(
    const ToolInvocationContext& invocation_context) {
  if (invocation_context.profile_snapshot != nullptr &&
      invocation_context.profile_snapshot->capability_cache_policy().stale_read_allowed) {
    return services::ServiceDataFreshness::allow_stale;
  }

  return services::ServiceDataFreshness::strict;
}

services::CapabilityTargetRef ToolServiceBridge::build_target(
    const contracts::ToolIR& tool_ir) {
  const auto capability_id = resolve_tool_name(tool_ir);
  return services::CapabilityTargetRef{
      .capability_id = capability_id,
      .target_id = default_target_id_for(capability_id),
  };
}

}  // namespace dasall::tools::bridge