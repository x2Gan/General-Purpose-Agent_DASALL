#include "mcp/MCPLane.h"

#include <chrono>
#include <utility>

#include "error/ResultCode.h"

namespace {

[[nodiscard]] std::int64_t current_time_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] dasall::contracts::ErrorInfo build_error(
    dasall::contracts::ResultCode code,
    std::string message,
    std::string stage,
    std::string ref_id) {
  return dasall::contracts::ErrorInfo{
      .failure_type = dasall::contracts::classify_result_code(code),
      .retryable = code == dasall::contracts::ResultCode::ProviderTimeout,
      .safe_to_replan = true,
      .details = dasall::contracts::ErrorDetails{
          .code = static_cast<int>(code),
          .message = std::move(message),
          .stage = std::move(stage),
      },
      .source_ref = dasall::contracts::ErrorSourceRefMinimal{
          .ref_type = "mcp.lane",
          .ref_id = std::move(ref_id),
      },
  };
}

[[nodiscard]] std::vector<std::string> build_tags(
    const std::optional<std::string>& lane_key,
    std::string_view server_id,
    std::string_view tag) {
  std::vector<std::string> tags{
      std::string("tool.executor.mcp"),
      std::string("server:") + std::string(server_id),
      std::string(tag),
  };
  if (lane_key.has_value() && !lane_key->empty()) {
    tags.push_back(std::string("lane:") + *lane_key);
  }
  return tags;
}

}  // namespace

namespace dasall::tools::mcp {

MCPLane::MCPLane()
    : MCPLane(default_dependencies()) {}

MCPLane::MCPLane(MCPLaneDependencies dependencies)
    : dependencies_(std::move(dependencies)) {}

contracts::ToolResult MCPLane::execute(
    const contracts::ToolIR& tool_ir,
    const ToolExecutionContext& execution_context,
    std::optional<std::string> preferred_server_id) const {
  if (!tool_ir.tool_name.has_value() || tool_ir.tool_name->empty()) {
    return build_failure_result(
        tool_ir,
        execution_context,
        "mcp.tool_name_missing",
        "tools.mcp.execute");
  }

  const auto binding = resolve_binding(
      *tool_ir.tool_name,
      preferred_server_id.has_value()
          ? std::optional<std::string_view>(*preferred_server_id)
          : std::nullopt);
  if (!binding.has_value()) {
    return build_failure_result(
        tool_ir,
        execution_context,
        "mcp.binding_missing",
        "tools.mcp.resolve_binding");
  }

  const auto session = ensure_session_ready(*binding);
  if (!session.ready()) {
    return build_failure_result(
        tool_ir,
        execution_context,
        "mcp.session_unavailable",
        "tools.mcp.ensure_session");
  }

  auto result = dependencies_.adapter->invoke(session, *binding, tool_ir);
  if (!result.tags.has_value()) {
    result.tags = build_tags(execution_context.lane_key, binding->server_id, "invoke");
  }
  return result;
}

std::optional<MCPToolBinding> MCPLane::resolve_binding(
    std::string_view tool_name,
    std::optional<std::string_view> preferred_server_id) const {
  if (!dependencies_.registry || tool_name.empty()) {
    return std::nullopt;
  }

  const auto bindings = dependencies_.registry->list_mcp_bindings(tool_name);
  if (bindings.empty()) {
    return std::nullopt;
  }

  if (preferred_server_id.has_value()) {
    for (const auto& binding : bindings) {
      if (binding.server_id == *preferred_server_id) {
        return binding;
      }
    }
  }

  return bindings.front();
}

MCPServerSession MCPLane::ensure_session_ready(const MCPToolBinding& binding) const {
  if (!dependencies_.adapter || !dependencies_.server_spec_resolver) {
    return MCPServerSession{};
  }

  const auto spec = dependencies_.server_spec_resolver(binding.server_id);
  if (!spec.has_value()) {
    return MCPServerSession{};
  }

  return dependencies_.adapter->ensure_session(*spec);
}

MCPLaneDependencies MCPLane::default_dependencies() {
  return MCPLaneDependencies{
      .registry = nullptr,
      .adapter = nullptr,
      .server_spec_resolver = {},
  };
}

contracts::ToolResult MCPLane::build_failure_result(
    const contracts::ToolIR& tool_ir,
    const ToolExecutionContext& execution_context,
    std::string reason_code,
    std::string stage) const {
  return contracts::ToolResult{
      .request_id = tool_ir.request_id,
      .tool_call_id = tool_ir.tool_call_id,
      .tool_name = tool_ir.tool_name,
      .success = false,
      .payload = std::nullopt,
      .error = build_error(
          contracts::ResultCode::ToolExecutionFailed,
          reason_code,
          std::move(stage),
          tool_ir.tool_name.value_or(std::string("unknown_tool"))),
      .side_effects = std::nullopt,
      .completed_at = current_time_ms(),
      .duration_ms = 0,
      .goal_id = tool_ir.goal_id,
      .worker_task_id = tool_ir.worker_task_id,
      .tags = build_tags(
          execution_context.lane_key,
          std::string_view("unknown"),
          reason_code),
  };
}

}  // namespace dasall::tools::mcp