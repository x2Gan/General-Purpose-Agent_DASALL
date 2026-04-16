#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mcp/StdioMCPServerLauncher.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

struct RecordingChannelState {
  std::string last_server_id;
  std::string last_command;
  bool closed = false;
};

class RecordingChannel final : public dasall::tools::mcp::IStdioTransportChannel {
 public:
  explicit RecordingChannel(std::shared_ptr<RecordingChannelState> state, std::string command)
      : state_(std::move(state)),
        command_(std::move(command)) {}

  [[nodiscard]] dasall::tools::mcp::TransportConnectResult open(
      const dasall::tools::mcp::MCPServerSpec& spec) override {
    state_->last_server_id = spec.server_id;
    state_->last_command = command_;
    connected_ = true;
    return dasall::tools::mcp::TransportConnectResult{
        .connected = true,
        .connection_id = std::string("conn-stdio-loopback"),
        .error = std::nullopt,
    };
  }

  void write(std::string_view json_rpc_message) override {
    last_write_ = std::string(json_rpc_message);
  }

  [[nodiscard]] std::optional<std::string> read(std::chrono::milliseconds timeout) override {
    static_cast<void>(timeout);
    return last_write_;
  }

  void close() override {
    connected_ = false;
    state_->closed = true;
  }

  [[nodiscard]] bool connected() const override {
    return connected_;
  }

 private:
  std::shared_ptr<RecordingChannelState> state_;
  std::string command_;
  std::string last_write_;
  bool connected_ = false;
};

[[nodiscard]] dasall::tools::bridge::MCPServerLaunchSpec make_launch_spec() {
  return dasall::tools::bridge::MCPServerLaunchSpec{
      .provider_ref = {
          .plugin_id = std::string("plugin.loopback"),
          .export_key = std::string("mcp.echo"),
          .source_revision = std::string("rev-a"),
      },
      .source_key = std::string("plugin:plugin.loopback"),
      .server_id = std::string("mcp.echo.loopback"),
      .launch_spec_ref = std::string("launch://plugin.loopback/mcp.echo.v1"),
      .trust_level = std::string("trusted-local"),
  };
}

[[nodiscard]] dasall::tools::mcp::StdioMCPLaunchSample make_launch_sample() {
  return dasall::tools::mcp::StdioMCPLaunchSample{
      .launch_spec_ref = std::string("launch://plugin.loopback/mcp.echo.v1"),
      .command = std::string("tests/mocks/bin/mcp-loopback-server"),
      .args = {"--scenario", "echo"},
      .env = {{"MCP_PROTOCOL_VERSION", "2025-03-26"}},
      .working_dir = std::string("${workspaceFolder}"),
      .protocol_version = std::string("2025-03-26"),
      .declared_capabilities = {"tools"},
      .tool_bindings = {
          dasall::tools::mcp::StdioLaunchBindingTemplate{
              .internal_tool_name = std::string("tool.echo"),
              .remote_tool_name = std::string("echo"),
              .remote_capability_id = std::string("cap.echo"),
              .input_schema_ref = std::string("schema://tool.echo/input"),
          },
      },
      .healthcheck_mode = std::string("initialize_roundtrip"),
  };
}

void test_launcher_builds_server_spec_and_bindings_from_launch_spec_ref() {
  const auto launch_sample = make_launch_sample();
  dasall::tools::mcp::StdioMCPServerLauncher launcher{
      dasall::tools::mcp::StdioMCPServerLauncherDependencies{
          .sample_resolver = [launch_sample](std::string_view launch_spec_ref)
              -> std::optional<dasall::tools::mcp::StdioMCPLaunchSample> {
            if (launch_spec_ref != launch_sample.launch_spec_ref) {
              return std::nullopt;
            }
            return launch_sample;
          },
          .channel_builder = {},
      }};

  const auto server_spec = launcher.build_server_spec(make_launch_spec());
  assert_true(server_spec.has_value(),
              "launcher should materialize an MCP server spec from launch_spec_ref");
  assert_equal(std::string("launch://plugin.loopback/mcp.echo.v1"),
               server_spec->endpoint_ref,
               "launcher should preserve the opaque launch_spec_ref as endpoint_ref");
  assert_equal(std::string("trusted-local"), server_spec->trust_level,
               "launcher should preserve trust level from the plugin bridge launch spec");
  assert_equal(std::string("initialize_roundtrip"),
               server_spec->healthcheck_ref.value_or(""),
               "launcher should expose the healthcheck mode from the parsed launch sample");

  const auto bindings = launcher.build_bindings(make_launch_spec());
  assert_equal(1, static_cast<int>(bindings.size()),
               "launcher should materialize one tool binding from the launch sample");
  assert_equal(std::string("tool.echo"), bindings.front().internal_tool_name,
               "launcher should preserve the internal tool binding name");
  assert_equal(std::string("echo"), bindings.front().remote_tool_name,
               "launcher should preserve the remote tool binding name");
}

void test_launcher_transport_factory_creates_stdio_transport_from_launch_sample() {
  const auto launch_sample = make_launch_sample();
  auto channel_state = std::make_shared<RecordingChannelState>();

  dasall::tools::mcp::StdioMCPServerLauncher launcher{
      dasall::tools::mcp::StdioMCPServerLauncherDependencies{
          .sample_resolver = [launch_sample](std::string_view launch_spec_ref)
              -> std::optional<dasall::tools::mcp::StdioMCPLaunchSample> {
            if (launch_spec_ref != launch_sample.launch_spec_ref) {
              return std::nullopt;
            }
            return launch_sample;
          },
          .channel_builder = [channel_state](
                                 const dasall::tools::mcp::MCPServerSpec& spec,
                                 const dasall::tools::mcp::StdioMCPLaunchSample& sample) {
            static_cast<void>(spec);
            return std::make_unique<RecordingChannel>(channel_state, sample.command);
          },
      }};

  const auto server_spec = launcher.build_server_spec(make_launch_spec());
  auto transport = launcher.build_transport_factory()(dasall::tools::mcp::MCPTransportKind::stdio);

  assert_true(server_spec.has_value(),
              "launcher transport test requires a valid server spec");
  assert_true(transport != nullptr,
              "launcher should provide a stdio transport factory for stdio servers");

  const auto connect_result = transport->connect(*server_spec);
  assert_true(connect_result.ok(),
              "launcher-provided stdio transport should connect through the injected channel builder");
  assert_equal(std::string("mcp.echo.loopback"), channel_state->last_server_id,
               "launcher transport should connect the resolved server id");
  assert_equal(std::string("tests/mocks/bin/mcp-loopback-server"), channel_state->last_command,
               "launcher transport should resolve the plugin-delivered launch command");

  transport->close();
  assert_true(channel_state->closed,
              "closing the stdio transport should close the injected channel");
}

}  // namespace

int main() {
  try {
    test_launcher_builds_server_spec_and_bindings_from_launch_spec_ref();
    test_launcher_transport_factory_creates_stdio_transport_from_launch_sample();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}