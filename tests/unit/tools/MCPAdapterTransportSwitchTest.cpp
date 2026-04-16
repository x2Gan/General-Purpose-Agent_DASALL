#include <chrono>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "mcp/MCPAdapter.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

struct TransportScript {
  dasall::tools::mcp::TransportConnectResult connect_result{
      .connected = true,
      .connection_id = std::string("conn-default"),
      .error = std::nullopt,
  };
  std::deque<std::string> responses;
  std::vector<std::string> sent_messages;
  bool connected = false;
  int close_calls = 0;
};

class ScriptedTransport final : public dasall::tools::mcp::IMCPTransport {
 public:
  explicit ScriptedTransport(std::shared_ptr<TransportScript> script)
      : script_(std::move(script)) {}

  [[nodiscard]] dasall::tools::mcp::TransportConnectResult connect(
      const dasall::tools::mcp::MCPServerSpec& spec) override {
    static_cast<void>(spec);
    script_->connected = script_->connect_result.ok();
    return script_->connect_result;
  }

  void send(std::string_view json_rpc_message) override {
    if (!script_->connected) {
      throw std::runtime_error("transport.not_connected");
    }
    script_->sent_messages.emplace_back(json_rpc_message);
  }

  [[nodiscard]] std::optional<std::string> receive(
      std::chrono::milliseconds timeout) override {
    static_cast<void>(timeout);
    if (!script_->connected || script_->responses.empty()) {
      return std::nullopt;
    }

    auto next = script_->responses.front();
    script_->responses.pop_front();
    return next;
  }

  void close() override {
    script_->connected = false;
    script_->close_calls += 1;
  }

  [[nodiscard]] bool is_connected() const override {
    return script_->connected;
  }

 private:
  std::shared_ptr<TransportScript> script_;
};

[[nodiscard]] dasall::tools::mcp::MCPServerSpec make_server_spec(
    dasall::tools::mcp::MCPTransportKind transport_kind,
    std::string endpoint_ref) {
  return dasall::tools::mcp::MCPServerSpec{
      .server_id = "mcp.echo",
      .transport_kind = transport_kind,
      .endpoint_ref = std::move(endpoint_ref),
      .declared_capabilities = {"tools"},
      .trust_level = "trusted",
      .healthcheck_ref = std::nullopt,
  };
}

[[nodiscard]] dasall::tools::mcp::MCPToolBinding make_binding() {
  return dasall::tools::mcp::MCPToolBinding{
      .internal_tool_name = "tool.echo",
      .remote_tool_name = "remote.echo",
      .server_id = "mcp.echo",
      .remote_capability_id = std::string("cap.echo"),
      .input_schema_ref = std::string("schema://input"),
  };
}

[[nodiscard]] dasall::contracts::ToolIR make_tool_ir() {
  return dasall::contracts::ToolIR{
      .request_id = std::string("req-1"),
      .tool_call_id = std::string("call-1"),
      .tool_name = std::string("tool.echo"),
      .operation = dasall::contracts::ToolIROperation::Invoke,
      .normalized_arguments = std::string("{}"),
      .route = dasall::contracts::ToolIRRoute::MCPRemote,
      .timeout_ms = 2000U,
      .idempotency_key = std::string("idem-1"),
      .priority = dasall::contracts::ToolIRPriority::Normal,
      .goal_id = std::string("goal-1"),
      .worker_task_id = std::string("worker-1"),
  };
}

[[nodiscard]] dasall::tools::mcp::MCPAdapter make_adapter(
    std::shared_ptr<TransportScript> stdio_script,
    std::shared_ptr<TransportScript> sse_script = nullptr) {
  return dasall::tools::mcp::MCPAdapter{
      dasall::tools::mcp::MCPAdapterDependencies{
          .transport_factory = [stdio_script, sse_script](dasall::tools::mcp::MCPTransportKind kind) {
            if (kind == dasall::tools::mcp::MCPTransportKind::stdio && stdio_script) {
              return std::shared_ptr<dasall::tools::mcp::IMCPTransport>(
                  std::make_shared<ScriptedTransport>(stdio_script));
            }
            if (kind == dasall::tools::mcp::MCPTransportKind::sse && sse_script) {
              return std::shared_ptr<dasall::tools::mcp::IMCPTransport>(
                  std::make_shared<ScriptedTransport>(sse_script));
            }
            return std::shared_ptr<dasall::tools::mcp::IMCPTransport>{};
          },
          .now_ms = []() { return static_cast<std::int64_t>(2000); },
          .session_ref_builder = [](std::string_view server_id, std::string_view connection_id) {
            return std::string(server_id) + ":session:" + std::string(connection_id);
          },
          .request_timeout = std::chrono::milliseconds(50),
      }};
}

void test_handshake_failure_returns_unready_session() {
  auto script = std::make_shared<TransportScript>();
  script->connect_result.connection_id = std::string("conn-stdio");
  script->responses = {
      "{\"jsonrpc\":\"2.0\",\"id\":\"initialize:mcp.echo\",\"error\":{\"message\":\"handshake rejected\"}}",
  };

  auto adapter = make_adapter(script);
  const auto session = adapter.ensure_session(
      make_server_spec(dasall::tools::mcp::MCPTransportKind::stdio, "loopback://stdio"));

  assert_true(!session.ready(), "handshake failure should fail closed with an unready session");
  assert_equal(1, script->close_calls,
               "handshake failure should close the underlying transport");
}

void test_transport_switch_closes_previous_connection() {
  auto stdio_script = std::make_shared<TransportScript>();
  stdio_script->connect_result.connection_id = std::string("conn-stdio");
  stdio_script->responses = {
      "{\"jsonrpc\":\"2.0\",\"id\":\"initialize:mcp.echo\",\"result\":{\"protocolVersion\":\"2025-03-26\"}}",
  };

  auto sse_script = std::make_shared<TransportScript>();
  sse_script->connect_result.connection_id = std::string("conn-sse");
  sse_script->responses = {
      "{\"jsonrpc\":\"2.0\",\"id\":\"initialize:mcp.echo\",\"result\":{\"protocolVersion\":\"2025-03-26\"}}",
  };

  auto adapter = make_adapter(stdio_script, sse_script);

  const auto stdio_session = adapter.ensure_session(
      make_server_spec(dasall::tools::mcp::MCPTransportKind::stdio, "loopback://stdio"));
  const auto sse_session = adapter.ensure_session(
      make_server_spec(dasall::tools::mcp::MCPTransportKind::sse, "https://mcp.echo/sse"));

  assert_true(stdio_session.ready(), "initial stdio session should succeed");
  assert_true(sse_session.ready(), "switching to a new transport kind should reconnect successfully");
  assert_true(sse_session.transport_kind == dasall::tools::mcp::MCPTransportKind::sse,
              "second session should preserve the selected transport kind");
  assert_equal(std::string("conn-sse"), sse_session.transport_connection_id.value_or(""),
               "second session should preserve the new connection id");
  assert_equal(1, stdio_script->close_calls,
               "transport switch should close the previous connection for the same server");
}

void test_protocol_error_is_mapped_to_failure_tool_result() {
  auto script = std::make_shared<TransportScript>();
  script->connect_result.connection_id = std::string("conn-stdio");
  script->responses = {
      "{\"jsonrpc\":\"2.0\",\"id\":\"initialize:mcp.echo\",\"result\":{\"protocolVersion\":\"2025-03-26\"}}",
      "{\"jsonrpc\":\"2.0\",\"id\":\"tools/call\",\"error\":{\"message\":\"remote unavailable\"}}",
  };

  auto adapter = make_adapter(script);
  const auto session = adapter.ensure_session(
      make_server_spec(dasall::tools::mcp::MCPTransportKind::stdio, "loopback://stdio"));
  const auto result = adapter.invoke(session, make_binding(), make_tool_ir());

  assert_true(!result.success.value_or(true),
              "protocol error should be mapped to a failed tool result");
  assert_equal(3001, result.error->details.code.value_or(0),
               "protocol error should map to ToolExecutionFailed");
  assert_equal(std::string("mcp.protocol_error:remote unavailable"),
               result.error->details.message,
               "protocol error should preserve the remote message in a stable reason string");
}

}  // namespace

int main() {
  try {
    test_handshake_failure_returns_unready_session();
    test_transport_switch_closes_previous_connection();
    test_protocol_error_is_mapped_to_failure_tool_result();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}