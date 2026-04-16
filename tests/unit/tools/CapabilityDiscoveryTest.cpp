#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mcp/CapabilityDiscovery.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class ScriptedAdapter final : public dasall::tools::mcp::IMCPAdapter {
 public:
  struct Behavior {
    dasall::tools::mcp::MCPServerSession session;
    dasall::tools::CapabilitySnapshot snapshot;
    int ensure_calls = 0;
    int list_calls = 0;
  };

  std::map<std::string, Behavior> behaviors_by_server;

  [[nodiscard]] dasall::tools::mcp::MCPServerSession ensure_session(
      const dasall::tools::mcp::MCPServerSpec& spec) override {
    auto it = behaviors_by_server.find(spec.server_id);
    if (it == behaviors_by_server.end()) {
      return {};
    }

    it->second.ensure_calls += 1;
    return it->second.session;
  }

  [[nodiscard]] dasall::tools::CapabilitySnapshot list_capabilities(
      const dasall::tools::mcp::MCPServerSession& session) override {
    auto it = behaviors_by_server.find(session.server_id);
    if (it == behaviors_by_server.end()) {
      return dasall::tools::CapabilitySnapshot{
          .server_id = session.server_id,
          .entries = {},
          .freshness = dasall::tools::CapabilityFreshness::expired,
          .last_refresh_at_ms = std::nullopt,
          .last_error = std::string("mcp.discovery.unconfigured_server"),
          .trust_marker = std::nullopt,
      };
    }

    it->second.list_calls += 1;
    return it->second.snapshot;
  }

  [[nodiscard]] dasall::contracts::ToolResult invoke(
      const dasall::tools::mcp::MCPServerSession& session,
      const dasall::tools::mcp::MCPToolBinding& binding,
      const dasall::contracts::ToolIR& tool_ir) override {
    static_cast<void>(session);
    static_cast<void>(binding);
    static_cast<void>(tool_ir);
    return {};
  }
};

[[nodiscard]] dasall::contracts::ToolDescriptor make_descriptor() {
  return dasall::contracts::ToolDescriptor{
      .tool_name = std::string("tool.echo"),
      .display_name = std::string("Echo"),
      .category = dasall::contracts::ToolCategory::Action,
      .capability_tier = dasall::contracts::ToolCapabilityTier::Stable,
      .is_read_only = false,
      .supports_compensation = false,
      .default_timeout_ms = 2500U,
      .input_schema_ref = std::string("schema://tool.echo/input"),
      .output_schema_ref = std::string("schema://tool.echo/output"),
      .required_scopes = std::vector<std::string>{"trusted"},
      .tags = std::vector<std::string>{"tool.route.mcp"},
      .version = std::string("1.0.0"),
  };
}

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

[[nodiscard]] dasall::tools::CapabilitySnapshot make_success_snapshot() {
  return dasall::tools::CapabilitySnapshot{
      .server_id = std::string("mcp.echo.loopback"),
      .entries = {
          dasall::tools::CapabilityEntry{
              .capability_id = std::string("cap.echo"),
              .capability_version = std::string("1.0.0"),
              .tool_names = {"tool.echo"},
          },
      },
      .freshness = dasall::tools::CapabilityFreshness::fresh,
      .last_refresh_at_ms = 1000,
      .last_error = std::nullopt,
      .trust_marker = std::nullopt,
  };
}

[[nodiscard]] std::shared_ptr<dasall::tools::mcp::StdioMCPServerLauncher> make_launcher() {
  const auto sample = make_launch_sample();
  return std::make_shared<dasall::tools::mcp::StdioMCPServerLauncher>(
      dasall::tools::mcp::StdioMCPServerLauncherDependencies{
          .sample_resolver = [sample](std::string_view launch_spec_ref)
              -> std::optional<dasall::tools::mcp::StdioMCPLaunchSample> {
            if (launch_spec_ref != sample.launch_spec_ref) {
              return std::nullopt;
            }
            return sample;
          },
          .channel_builder = {},
      });
}

void test_plugin_delta_schedules_immediate_refresh_and_publishes_bindings() {
  std::int64_t now_ms = 1000;
  auto cache = std::make_shared<dasall::tools::mcp::CapabilityCache>(
      dasall::tools::mcp::CapabilityCacheOptions{
          .expire_after_ms = 5000,
          .stale_read_allowed = true,
          .now_ms = [&now_ms]() { return now_ms; },
      });
  auto registry = std::make_shared<dasall::tools::registry::ToolRegistry>(
      std::vector<dasall::contracts::ToolDescriptor>{make_descriptor()});
  auto adapter = std::make_shared<ScriptedAdapter>();
  adapter->behaviors_by_server["mcp.echo.loopback"] = ScriptedAdapter::Behavior{
      .session = {
          .server_id = std::string("mcp.echo.loopback"),
          .transport_kind = dasall::tools::mcp::MCPTransportKind::stdio,
          .session_ref = std::string("sess-echo"),
          .negotiated_protocol_version = std::string("2025-03-26"),
          .transport_connection_id = std::string("conn-echo"),
      },
      .snapshot = make_success_snapshot(),
  };

  dasall::tools::mcp::CapabilityDiscovery discovery{
      dasall::tools::mcp::CapabilityDiscoveryDependencies{
          .capability_cache = cache,
          .adapter = adapter,
          .registry = registry,
          .launcher = make_launcher(),
          .now_ms = [&now_ms]() { return now_ms; },
          .refresh_interval_ms = 500,
          .failure_backoff_ms = 200,
      }};

  assert_true(
      discovery.on_plugin_delta("plugin:plugin.loopback", {make_launch_spec()}),
      "plugin delta should be accepted when launch_spec_ref resolves into a valid launch sample");
  assert_equal(1, static_cast<int>(discovery.schedule_refresh().size()),
               "new plugin-delivered servers should be refreshable immediately");
  assert_true(discovery.resolve_server_spec("mcp.echo.loopback").has_value(),
              "accepted plugin delta should publish the resolved server spec");

  const auto summary = discovery.refresh_once();
  assert_equal(1, static_cast<int>(summary.refreshed_server_ids.size()),
               "successful discovery refresh should report one refreshed server");

  const auto bindings = registry->list_mcp_bindings("tool.echo");
  assert_equal(1, static_cast<int>(bindings.size()),
               "successful discovery refresh should publish the launch-sample bindings");
  assert_equal(std::string("mcp.echo.loopback"), bindings.front().server_id,
               "published binding should point at the discovered MCP server id");

  const auto cached_snapshot = cache->snapshot("mcp.echo.loopback");
  assert_true(cached_snapshot.has_value(),
              "successful discovery refresh should populate the capability cache");
  assert_true(cached_snapshot->freshness == dasall::tools::CapabilityFreshness::fresh,
              "successful discovery refresh should leave the cache snapshot fresh");
  assert_equal(std::string("trusted-local"), cached_snapshot->trust_marker.value_or(""),
               "discovery should inherit trust level from the resolved server spec when the adapter omits it");

  assert_equal(0, static_cast<int>(discovery.schedule_refresh().size()),
               "refresh interval should postpone the next refresh after a successful run");
}

void test_refresh_failure_applies_backoff_and_retains_stale_snapshot() {
  std::int64_t now_ms = 2000;
  auto cache = std::make_shared<dasall::tools::mcp::CapabilityCache>(
      dasall::tools::mcp::CapabilityCacheOptions{
          .expire_after_ms = 5000,
          .stale_read_allowed = true,
          .now_ms = [&now_ms]() { return now_ms; },
      });
  auto registry = std::make_shared<dasall::tools::registry::ToolRegistry>(
      std::vector<dasall::contracts::ToolDescriptor>{make_descriptor()});
  auto adapter = std::make_shared<ScriptedAdapter>();
  adapter->behaviors_by_server["mcp.echo.loopback"] = ScriptedAdapter::Behavior{
      .session = {
          .server_id = std::string("mcp.echo.loopback"),
          .transport_kind = dasall::tools::mcp::MCPTransportKind::stdio,
          .session_ref = std::string("sess-echo"),
          .negotiated_protocol_version = std::string("2025-03-26"),
          .transport_connection_id = std::string("conn-echo"),
      },
      .snapshot = dasall::tools::CapabilitySnapshot{
          .server_id = std::string("mcp.echo.loopback"),
          .entries = {},
          .freshness = dasall::tools::CapabilityFreshness::expired,
          .last_refresh_at_ms = 1900,
          .last_error = std::string("mcp.discovery.timeout"),
          .trust_marker = std::nullopt,
      },
  };

  cache->update(make_success_snapshot());

  dasall::tools::mcp::CapabilityDiscovery discovery{
      dasall::tools::mcp::CapabilityDiscoveryDependencies{
          .capability_cache = cache,
          .adapter = adapter,
          .registry = registry,
          .launcher = make_launcher(),
          .now_ms = [&now_ms]() { return now_ms; },
          .refresh_interval_ms = 500,
          .failure_backoff_ms = 250,
      }};

  assert_true(
      discovery.on_plugin_delta("plugin:plugin.loopback", {make_launch_spec()}),
      "failure-path test requires a valid plugin delta before refresh");

  const auto summary = discovery.refresh_once();
  assert_equal(1, static_cast<int>(summary.failed_server_ids.size()),
               "refresh failure should be reported against the affected server");

  const auto cached_snapshot = cache->snapshot("mcp.echo.loopback");
  assert_true(cached_snapshot.has_value(),
              "failed refresh should retain the previous cache snapshot instead of dropping it");
  assert_true(cached_snapshot->freshness == dasall::tools::CapabilityFreshness::stale,
              "failed refresh should downgrade the previous trusted snapshot to stale");
  assert_equal(std::string("mcp.discovery.timeout"), cached_snapshot->last_error.value_or(""),
               "failed refresh should preserve the most recent discovery error in cache");

  assert_equal(0, static_cast<int>(discovery.schedule_refresh().size()),
               "failure backoff should suppress immediate re-refresh attempts");

  now_ms += 250;
  assert_equal(1, static_cast<int>(discovery.schedule_refresh().size()),
               "after the backoff window expires the server should become refreshable again");
}

}  // namespace

int main() {
  try {
    test_plugin_delta_schedules_immediate_refresh_and_publishes_bindings();
    test_refresh_failure_applies_backoff_and_retains_stale_snapshot();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}