#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "mcp/MCPAdapter.h"
#include "mcp/StdioMCPTransport.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

struct ScriptedChannelState {
  dasall::tools::mcp::TransportConnectResult connect_result{
      .connected = true,
      .connection_id = std::string("conn-stdio"),
      .error = std::nullopt,
  };
  std::deque<std::string> responses;
  std::vector<std::string> sent_messages;
  bool connected = false;
  int close_calls = 0;
};

class ScriptedStdioChannel final : public dasall::tools::mcp::IStdioTransportChannel {
 public:
  explicit ScriptedStdioChannel(std::shared_ptr<ScriptedChannelState> state)
      : state_(std::move(state)) {}

  [[nodiscard]] dasall::tools::mcp::TransportConnectResult open(
      const dasall::tools::mcp::MCPServerSpec& spec) override {
    static_cast<void>(spec);
    state_->connected = state_->connect_result.ok();
    return state_->connect_result;
  }

  void write(std::string_view json_rpc_message) override {
    if (!state_->connected) {
      throw std::runtime_error("channel.not_connected");
    }
    state_->sent_messages.emplace_back(json_rpc_message);
  }

  [[nodiscard]] std::optional<std::string> read(
      std::chrono::milliseconds timeout) override {
    static_cast<void>(timeout);
    if (!state_->connected || state_->responses.empty()) {
      return std::nullopt;
    }

    auto next = state_->responses.front();
    state_->responses.pop_front();
    return next;
  }

  void close() override {
    state_->connected = false;
    state_->close_calls += 1;
  }

  [[nodiscard]] bool connected() const override {
    return state_->connected;
  }

 private:
  std::shared_ptr<ScriptedChannelState> state_;
};

[[nodiscard]] dasall::tools::mcp::MCPServerSpec make_server_spec() {
  return dasall::tools::mcp::MCPServerSpec{
      .server_id = "mcp.echo",
      .transport_kind = dasall::tools::mcp::MCPTransportKind::stdio,
      .endpoint_ref = "loopback://mcp.echo",
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

void test_adapter_uses_stdio_transport_for_handshake_list_and_invoke() {
  auto state = std::make_shared<ScriptedChannelState>();
  state->responses = {
      "{\"jsonrpc\":\"2.0\",\"id\":\"initialize:mcp.echo\",\"result\":{\"protocolVersion\":\"2025-03-26\"}}",
      "{\"jsonrpc\":\"2.0\",\"id\":\"tools/list\",\"result\":{\"tools\":[{\"name\":\"remote.echo\",\"capabilityId\":\"cap.echo\"}]}}",
      "{\"jsonrpc\":\"2.0\",\"id\":\"tools/call\",\"result\":{\"payload\":\"ok\"}}",
  };

  dasall::tools::mcp::MCPAdapter adapter{
      dasall::tools::mcp::MCPAdapterDependencies{
          .transport_factory = [state](dasall::tools::mcp::MCPTransportKind kind) {
            if (kind != dasall::tools::mcp::MCPTransportKind::stdio) {
              return std::shared_ptr<dasall::tools::mcp::IMCPTransport>{};
            }

            return std::shared_ptr<dasall::tools::mcp::IMCPTransport>(
                std::make_shared<dasall::tools::mcp::StdioMCPTransport>(
                    [state](const dasall::tools::mcp::MCPServerSpec& spec) {
                      static_cast<void>(spec);
                      return std::make_unique<ScriptedStdioChannel>(state);
                    }));
          },
          .now_ms = []() { return static_cast<std::int64_t>(1000); },
          .session_ref_builder = [](std::string_view server_id, std::string_view connection_id) {
            return std::string(server_id) + ":session:" + std::string(connection_id);
          },
          .request_timeout = std::chrono::milliseconds(50),
      }};

  const auto session = adapter.ensure_session(make_server_spec());
  assert_true(session.ready(), "stdio transport should complete the initialize handshake");
  assert_equal(std::string("2025-03-26"), session.negotiated_protocol_version.value_or(""),
               "handshake should preserve the negotiated protocol version");
  assert_equal(std::string("conn-stdio"), session.transport_connection_id.value_or(""),
               "session should keep the transport connection id");

  const auto snapshot = adapter.list_capabilities(session);
  assert_true(snapshot.freshness == dasall::tools::CapabilityFreshness::fresh,
              "tools/list should produce a fresh capability snapshot");
  assert_equal(std::string("trusted"), snapshot.trust_marker.value_or(""),
               "trust level should be projected into the capability snapshot");
  assert_equal(1, static_cast<int>(snapshot.entries.size()),
               "tools/list should produce one capability entry for the scripted server");
  assert_equal(std::string("remote.echo"), snapshot.entries.front().tool_names.front(),
               "tools/list should preserve the remote tool name");

  const auto result = adapter.invoke(session, make_binding(), make_tool_ir());
  assert_true(result.success.value_or(false),
              "tools/call should succeed when the scripted response returns a result payload");
  assert_equal(std::string("ok"), result.payload.value_or(""),
               "tools/call should preserve the remote payload");

  assert_equal(4, static_cast<int>(state->sent_messages.size()),
               "adapter should send initialize, initialized, tools/list and tools/call");
  assert_true(state->sent_messages[0].find("initialize") != std::string::npos,
              "first message should be initialize");
  assert_true(state->sent_messages[1].find("notifications/initialized") != std::string::npos,
              "second message should be the initialized notification");
  assert_true(state->sent_messages[2].find("tools/list") != std::string::npos,
              "third message should request tools/list");
  assert_true(state->sent_messages[3].find("tools/call") != std::string::npos,
              "fourth message should request tools/call");
}

}  // namespace

int main() {
  try {
    test_adapter_uses_stdio_transport_for_handshake_list_and_invoke();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}