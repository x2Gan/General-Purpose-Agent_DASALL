#include "mcp/MCPAdapter.h"

#include <chrono>
#include <exception>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "error/ResultCode.h"
#include "mcp/StdioMCPTransport.h"

namespace {

using dasall::contracts::ErrorDetails;
using dasall::contracts::ErrorInfo;
using dasall::contracts::ErrorSourceRefMinimal;
using dasall::contracts::ResultCode;
using dasall::contracts::ToolIR;
using dasall::contracts::ToolResult;
using dasall::tools::CapabilityEntry;
using dasall::tools::CapabilityFreshness;
using dasall::tools::CapabilitySnapshot;
using dasall::tools::mcp::MCPServerSession;

[[nodiscard]] std::int64_t current_time_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] ErrorInfo build_error(
    ResultCode code,
    std::string message,
    std::string stage,
    std::string ref_id) {
  return ErrorInfo{
      .failure_type = dasall::contracts::classify_result_code(code),
      .retryable = code == ResultCode::ProviderTimeout ||
                   code == ResultCode::RuntimeRetryExhausted,
      .safe_to_replan = code != ResultCode::ProviderTimeout,
      .details = ErrorDetails{
          .code = static_cast<int>(code),
          .message = std::move(message),
          .stage = std::move(stage),
      },
      .source_ref = ErrorSourceRefMinimal{
          .ref_type = "mcp.adapter",
          .ref_id = std::move(ref_id),
      },
  };
}

[[nodiscard]] std::string quote(std::string_view value) {
  return std::string("\"") + std::string(value) + "\"";
}

[[nodiscard]] std::optional<std::string> extract_string_field(
    std::string_view payload,
    std::string_view key,
    std::size_t start_at = 0U) {
  const auto needle = quote(key);
  const auto key_pos = payload.find(needle, start_at);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }

  const auto colon_pos = payload.find(':', key_pos + needle.size());
  if (colon_pos == std::string_view::npos) {
    return std::nullopt;
  }

  const auto open_quote = payload.find('"', colon_pos + 1U);
  if (open_quote == std::string_view::npos) {
    return std::nullopt;
  }

  const auto close_quote = payload.find('"', open_quote + 1U);
  if (close_quote == std::string_view::npos) {
    return std::nullopt;
  }

  return std::string(payload.substr(open_quote + 1U, close_quote - open_quote - 1U));
}

[[nodiscard]] std::vector<std::string> extract_all_string_fields(
    std::string_view payload,
    std::string_view key) {
  std::vector<std::string> values;
  std::size_t search_start = 0U;

  while (search_start < payload.size()) {
    const auto value = extract_string_field(payload, key, search_start);
    if (!value.has_value()) {
      break;
    }

    values.push_back(*value);
    const auto needle = quote(key);
    const auto key_pos = payload.find(needle, search_start);
    if (key_pos == std::string_view::npos) {
      break;
    }
    search_start = key_pos + needle.size();
  }

  return values;
}

[[nodiscard]] bool contains_key(std::string_view payload, std::string_view key) {
  return payload.find(quote(key)) != std::string_view::npos;
}

[[nodiscard]] std::optional<std::string> extract_protocol_version(
    std::string_view payload) {
  if (const auto camel = extract_string_field(payload, "protocolVersion"); camel.has_value()) {
    return camel;
  }
  return extract_string_field(payload, "protocol_version");
}

[[nodiscard]] std::string extract_error_message(std::string_view payload) {
  if (const auto message = extract_string_field(payload, "message"); message.has_value()) {
    return *message;
  }
  return std::string("mcp.protocol_error");
}

[[nodiscard]] std::optional<std::string> normalize_trust_marker(std::string_view trust_level) {
  if (trust_level.empty()) {
    return std::nullopt;
  }
  return std::string(trust_level);
}

[[nodiscard]] std::vector<std::string> build_tags(
    std::string_view server_id,
    std::string_view tag) {
  return {
      std::string("tool.executor.mcp"),
      std::string("server:") + std::string(server_id),
      std::string(tag),
  };
}

[[nodiscard]] CapabilitySnapshot build_failure_snapshot(
    std::string server_id,
    std::optional<std::string> trust_marker,
    std::string error_message) {
  return CapabilitySnapshot{
      .server_id = std::move(server_id),
      .entries = {},
      .freshness = CapabilityFreshness::expired,
      .last_refresh_at_ms = std::nullopt,
      .last_error = std::move(error_message),
      .trust_marker = std::move(trust_marker),
  };
}

[[nodiscard]] std::string build_initialize_request(
    const dasall::tools::mcp::MCPServerSpec& spec) {
  return std::string("{\"jsonrpc\":\"2.0\",\"id\":\"initialize:") + spec.server_id +
         "\",\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-03-26\",\"clientInfo\":{\"name\":\"dasall-tools\",\"version\":\"v1\"}}}";
}

[[nodiscard]] std::string build_initialized_notification() {
  return std::string(
      "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\",\"params\":{}}"
  );
}

[[nodiscard]] std::string build_tools_list_request(const MCPServerSession& session) {
  return std::string("{\"jsonrpc\":\"2.0\",\"id\":\"tools/list:") + session.session_ref +
         "\",\"method\":\"tools/list\",\"params\":{}}";
}

[[nodiscard]] std::string build_tools_call_request(
    const MCPServerSession& session,
    const dasall::tools::mcp::MCPToolBinding& binding,
    const ToolIR& tool_ir) {
  const auto arguments = tool_ir.normalized_arguments.value_or(std::string("{}"));
  return std::string("{\"jsonrpc\":\"2.0\",\"id\":\"tools/call:") + session.session_ref +
         "\",\"method\":\"tools/call\",\"params\":{\"name\":" + quote(binding.remote_tool_name) +
         ",\"arguments\":" + arguments + "}}";
}

[[nodiscard]] std::vector<CapabilityEntry> parse_capability_entries(std::string_view payload) {
  const auto tool_names = extract_all_string_fields(payload, "name");
  auto capability_ids = extract_all_string_fields(payload, "capabilityId");
  if (capability_ids.empty()) {
    capability_ids = extract_all_string_fields(payload, "capability_id");
  }

  std::vector<CapabilityEntry> entries;
  entries.reserve(tool_names.size());

  for (std::size_t index = 0; index < tool_names.size(); ++index) {
    entries.push_back(CapabilityEntry{
        .capability_id = capability_ids.size() > index
                             ? capability_ids[index]
                             : std::string("cap.") + tool_names[index],
        .capability_version = std::string("1.0"),
        .tool_names = {tool_names[index]},
    });
  }

  return entries;
}

[[nodiscard]] std::string extract_invoke_payload(std::string_view payload) {
  if (const auto direct = extract_string_field(payload, "payload"); direct.has_value()) {
    return *direct;
  }
  if (const auto content = extract_string_field(payload, "content"); content.has_value()) {
    return *content;
  }
  return std::string(payload);
}

}  // namespace

namespace dasall::tools::mcp {

MCPAdapter::MCPAdapter()
    : MCPAdapter(default_dependencies()) {}

MCPAdapter::MCPAdapter(MCPAdapterDependencies dependencies)
    : dependencies_(std::move(dependencies)) {
  const auto defaults = default_dependencies();
  if (!dependencies_.transport_factory) {
    dependencies_.transport_factory = defaults.transport_factory;
  }
  if (!dependencies_.now_ms) {
    dependencies_.now_ms = defaults.now_ms;
  }
  if (!dependencies_.session_ref_builder) {
    dependencies_.session_ref_builder = defaults.session_ref_builder;
  }
}

MCPServerSession MCPAdapter::ensure_session(const MCPServerSpec& spec) {
  {
    std::lock_guard<std::mutex> guard(sessions_mutex_);
    const auto existing = sessions_by_server_.find(spec.server_id);
    if (existing != sessions_by_server_.end() && existing->second.transport &&
        existing->second.transport->is_connected() &&
        existing->second.spec.transport_kind == spec.transport_kind &&
        existing->second.spec.endpoint_ref == spec.endpoint_ref &&
        existing->second.session.ready()) {
      return existing->second.session;
    }
  }

  const auto transport = select_transport(spec.transport_kind);
  if (!transport) {
    return MCPServerSession{};
  }

  const auto connect_result = transport->connect(spec);
  if (!connect_result.ok()) {
    transport->close();
    return MCPServerSession{};
  }

  const auto session = perform_handshake(spec, connect_result, transport);
  if (!session.ready()) {
    transport->close();
    return MCPServerSession{};
  }

  std::lock_guard<std::mutex> guard(sessions_mutex_);
  auto existing = sessions_by_server_.find(spec.server_id);
  if (existing != sessions_by_server_.end() && existing->second.transport &&
      existing->second.transport != transport) {
    existing->second.transport->close();
  }
  sessions_by_server_[spec.server_id] = SessionRecord{
      .spec = spec,
      .session = session,
      .transport = transport,
  };
  return session;
}

CapabilitySnapshot MCPAdapter::list_capabilities(const MCPServerSession& session) {
  const auto record = resolve_session_record(session);
  if (!record.has_value() || !record->transport || !record->transport->is_connected()) {
    return build_failure_snapshot(
        session.server_id,
        std::nullopt,
        std::string("mcp.session_unavailable"));
  }

  try {
    record->transport->send(build_tools_list_request(session));
  } catch (const std::exception& ex) {
    return build_failure_snapshot(
        session.server_id,
        normalize_trust_marker(record->spec.trust_level),
        std::string("mcp.transport_send_failed:") + ex.what());
  }

  const auto response = record->transport->receive(dependencies_.request_timeout);
  if (!response.has_value()) {
    return build_failure_snapshot(
        session.server_id,
        normalize_trust_marker(record->spec.trust_level),
        std::string("mcp.timeout"));
  }

  if (contains_key(*response, "error")) {
    return build_failure_snapshot(
        session.server_id,
        normalize_trust_marker(record->spec.trust_level),
        std::string("mcp.protocol_error:") + extract_error_message(*response));
  }

  return CapabilitySnapshot{
      .server_id = session.server_id,
      .entries = parse_capability_entries(*response),
      .freshness = CapabilityFreshness::fresh,
      .last_refresh_at_ms = current_time_ms(),
      .last_error = std::nullopt,
      .trust_marker = normalize_trust_marker(record->spec.trust_level),
  };
}

ToolResult MCPAdapter::invoke(
    const MCPServerSession& session,
    const MCPToolBinding& binding,
    const ToolIR& tool_ir) {
  const auto record = resolve_session_record(session);
  if (!record.has_value() || !record->transport || !record->transport->is_connected()) {
    return map_protocol_error(
        session,
        tool_ir,
        "mcp.session_unavailable",
        "mcp.session_unavailable",
        ResultCode::ToolExecutionFailed);
  }

  try {
    record->transport->send(build_tools_call_request(session, binding, tool_ir));
  } catch (const std::exception& ex) {
    return map_protocol_error(
        session,
        tool_ir,
        "mcp.transport_send_failed",
        std::string("mcp.transport_send_failed:") + ex.what(),
        ResultCode::ToolExecutionFailed);
  }

  const auto response = record->transport->receive(dependencies_.request_timeout);
  if (!response.has_value()) {
    return map_protocol_error(
        session,
        tool_ir,
        "mcp.timeout",
        "mcp.timeout",
        ResultCode::ProviderTimeout);
  }

  if (contains_key(*response, "error")) {
    return map_protocol_error(
        session,
        tool_ir,
        "mcp.protocol_error",
        std::string("mcp.protocol_error:") + extract_error_message(*response),
        ResultCode::ToolExecutionFailed);
  }

  return ToolResult{
      .request_id = tool_ir.request_id,
      .tool_call_id = tool_ir.tool_call_id,
      .tool_name = tool_ir.tool_name,
      .success = true,
      .payload = extract_invoke_payload(*response),
      .error = std::nullopt,
      .side_effects = std::nullopt,
      .completed_at = current_time_ms(),
      .duration_ms = 0,
      .goal_id = tool_ir.goal_id,
      .worker_task_id = tool_ir.worker_task_id,
      .tags = build_tags(session.server_id, "invoke"),
  };
}

MCPAdapterDependencies MCPAdapter::default_dependencies() {
  return MCPAdapterDependencies{
      .transport_factory = [](MCPTransportKind kind) -> std::shared_ptr<IMCPTransport> {
        if (kind == MCPTransportKind::stdio) {
          return std::make_shared<StdioMCPTransport>();
        }
        return nullptr;
      },
      .now_ms = []() { return ::current_time_ms(); },
      .session_ref_builder = [](std::string_view server_id, std::string_view connection_id) {
        return std::string(server_id) + ":session:" + std::string(connection_id);
      },
      .request_timeout = std::chrono::milliseconds(250),
  };
}

std::shared_ptr<IMCPTransport> MCPAdapter::select_transport(MCPTransportKind kind) const {
  return dependencies_.transport_factory ? dependencies_.transport_factory(kind) : nullptr;
}

std::int64_t MCPAdapter::current_time_ms() const {
  return dependencies_.now_ms ? dependencies_.now_ms() : ::current_time_ms();
}

std::string MCPAdapter::build_session_ref(
    std::string_view server_id,
    std::string_view connection_id) const {
  return dependencies_.session_ref_builder
             ? dependencies_.session_ref_builder(server_id, connection_id)
             : std::string(server_id) + ":session:" + std::string(connection_id);
}

MCPServerSession MCPAdapter::perform_handshake(
    const MCPServerSpec& spec,
    const TransportConnectResult& connect_result,
    const std::shared_ptr<IMCPTransport>& transport) const {
  try {
    transport->send(build_initialize_request(spec));
  } catch (const std::exception&) {
    return MCPServerSession{};
  }

  const auto initialize_response = transport->receive(dependencies_.request_timeout);
  if (!initialize_response.has_value() || contains_key(*initialize_response, "error")) {
    return MCPServerSession{};
  }

  const auto protocol_version = extract_protocol_version(*initialize_response);
  if (!protocol_version.has_value()) {
    return MCPServerSession{};
  }

  try {
    transport->send(build_initialized_notification());
  } catch (const std::exception&) {
    return MCPServerSession{};
  }

  return MCPServerSession{
      .server_id = spec.server_id,
      .transport_kind = spec.transport_kind,
      .session_ref = build_session_ref(
          spec.server_id,
          connect_result.connection_id.value_or(std::string("connection"))),
      .negotiated_protocol_version = protocol_version,
      .transport_connection_id = connect_result.connection_id,
  };
}

std::optional<MCPAdapter::SessionRecord> MCPAdapter::resolve_session_record(
    const MCPServerSession& session) const {
  std::lock_guard<std::mutex> guard(sessions_mutex_);
  const auto found = sessions_by_server_.find(session.server_id);
  if (found == sessions_by_server_.end()) {
    return std::nullopt;
  }
  if (found->second.session.session_ref != session.session_ref) {
    return std::nullopt;
  }
  return found->second;
}

ToolResult MCPAdapter::map_protocol_error(
    const MCPServerSession& session,
    const ToolIR& tool_ir,
    std::string reason_code,
    std::string message,
    ResultCode result_code) const {
  return ToolResult{
      .request_id = tool_ir.request_id,
      .tool_call_id = tool_ir.tool_call_id,
      .tool_name = tool_ir.tool_name,
      .success = false,
      .payload = std::nullopt,
      .error = build_error(
          result_code,
          std::move(message),
          "tools.mcp.invoke",
          session.server_id.empty() ? std::string("unknown_server") : session.server_id),
      .side_effects = std::nullopt,
      .completed_at = current_time_ms(),
      .duration_ms = 0,
      .goal_id = tool_ir.goal_id,
      .worker_task_id = tool_ir.worker_task_id,
      .tags = build_tags(
          session.server_id.empty() ? std::string_view("unknown")
                                    : std::string_view(session.server_id),
          reason_code),
  };
}

}  // namespace dasall::tools::mcp