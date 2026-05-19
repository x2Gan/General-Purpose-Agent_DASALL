#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "MCPLoopbackServerFixture.h"
#include "RuntimePolicySnapshot.h"
#include "ToolManager.h"
#include "bridge/ToolPluginExtensionConsumer.h"
#include "execution/BuiltinExecutorLane.h"
#include "mcp/CapabilityCache.h"
#include "mcp/CapabilityDiscovery.h"
#include "mcp/MCPAdapter.h"
#include "mcp/MCPLane.h"
#include "mcp/StdioMCPServerLauncher.h"
#include "registry/ToolRegistry.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

constexpr std::string_view kPluginId = "plugin.loopback";
constexpr std::string_view kSourceKey = "plugin:plugin.loopback";
constexpr std::string_view kServerId = "mcp.echo.loopback";

[[nodiscard]] bool contains_fragment(std::string_view value, std::string_view fragment) {
  return value.find(fragment) != std::string_view::npos;
}

[[nodiscard]] dasall::contracts::ToolDescriptor make_descriptor() {
  return dasall::contracts::ToolDescriptor{
      .tool_name = std::string("tool.echo"),
      .display_name = std::string("Tool Echo"),
      .category = dasall::contracts::ToolCategory::Action,
      .capability_tier = dasall::contracts::ToolCapabilityTier::Stable,
      .is_read_only = false,
      .supports_compensation = true,
      .default_timeout_ms = 2500U,
      .input_schema_ref = std::string("schema://tools/tool.echo/input/v1"),
      .output_schema_ref = std::string("schema://tools/tool.echo/output/v1"),
      .required_scopes = std::vector<std::string>{"trusted"},
      .tags = std::vector<std::string>{"builtin", "mcp"},
      .version = std::string("1.0.0"),
  };
}

[[nodiscard]] dasall::contracts::ToolRequest make_request() {
  return dasall::contracts::ToolRequest{
      .request_id = std::string("req-tool-plugin-stdio"),
      .tool_call_id = std::string("call-tool-plugin-stdio"),
      .tool_name = std::string("tool.echo"),
      .invocation_kind = dasall::contracts::ToolInvocationKind::Action,
      .arguments_payload = std::string("{\"message\":\"plugin\"}"),
      .created_at = 6000,
      .goal_id = std::string("goal-tool-plugin-stdio"),
      .worker_task_id = std::string("worker-tool-plugin-stdio"),
      .runtime_budget = std::nullopt,
      .timeout_ms = 2500U,
      .idempotency_key = std::string("idem-tool-plugin-stdio"),
      .tags = std::vector<std::string>{"integration", "tools", "tool.route.mcp"},
  };
}

[[nodiscard]] dasall::profiles::BuildProfileManifest make_manifest() {
  return dasall::profiles::BuildProfileManifest{
      .enabled_modules = {"runtime", "tools_builtin", "tools_mcp"},
      .enabled_adapters = {},
      .observability_level = "minimal",
      .build_tags = {"tools:manager", "tools:mcp"},
      .toolchain_hint = std::string("x86_64-linux-gnu"),
  };
}

[[nodiscard]] dasall::profiles::RuntimePolicySnapshot make_snapshot() {
  return dasall::profiles::RuntimePolicySnapshot{
      1U,
      "desktop_full",
      dasall::contracts::RuntimeBudget{
          .max_tokens = 4096U,
          .max_turns = 8U,
          .max_tool_calls = 24U,
          .max_latency_ms = 8000U,
          .max_replan_count = 2U,
      },
      dasall::profiles::ModelProfile{
          .stage_routes = {{
              "planner",
              dasall::profiles::ModelRoutePolicy{
                  .route = "local.small",
                  .fallback_route = std::string("builtin_only"),
                  .streaming_enabled = false,
              },
          }},
      },
      dasall::profiles::TokenBudgetPolicy{
          .max_input_tokens = 1024U,
          .max_output_tokens = 512U,
          .max_history_turns = 4U,
          .compression_threshold = 768U,
      },
      dasall::profiles::PromptPolicy{
          .allowed_prompt_releases = {"stable"},
          .trusted_sources = {"profiles"},
          .tool_visibility_rules = {"builtin:all", "mcp:all"},
      },
      dasall::profiles::CapabilityCachePolicy{
          .refresh_interval_ms = 10000,
          .expire_after_ms = 180000,
          .stale_read_allowed = true,
          .failure_backoff_ms = 5000,
      },
      dasall::profiles::DegradePolicy{
          .fallback_chain = {"builtin_only"},
          .allow_model_failover = false,
          .allow_budget_degrade = true,
      },
      dasall::profiles::TimeoutPolicy{
          .llm = dasall::profiles::TimeoutBudget{
              .timeout_ms = 1800,
              .retry_budget = 0U,
              .circuit_breaker_threshold = 3U,
          },
          .tool = dasall::profiles::TimeoutBudget{
              .timeout_ms = 2500,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 4U,
          },
          .mcp = dasall::profiles::TimeoutBudget{
              .timeout_ms = 2000,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 3U,
          },
          .workflow = dasall::profiles::TimeoutBudget{
              .timeout_ms = 5000,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 3U,
          },
      },
      dasall::profiles::ExecutionPolicy{
          .requires_high_risk_confirmation = false,
          .safe_mode_enabled = true,
          .audit_level = "full",
          .allowed_tool_domains = {"builtin", "mcp"},
      },
      dasall::profiles::OpsPolicy{
          .log_level = "info",
          .metrics_granularity = "full",
          .trace_sample_ratio = 0.5,
          .remote_diagnostics_enabled = false,
          .upgrade_strategy = "rolling",
      },
      4U};
}

[[nodiscard]] dasall::tools::ToolInvocationContext make_context(
    const dasall::profiles::RuntimePolicySnapshot& snapshot) {
  return dasall::tools::ToolInvocationContext{
      .caller_domain = std::string("runtime.main"),
      .session_id = std::string("session-tool-plugin-stdio"),
      .profile_snapshot = &snapshot,
      .trace = {
          .trace_id = std::string("trace-tool-plugin-stdio"),
          .span_id = std::string("span-tool-plugin-stdio"),
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::nullopt,
  };
}

[[nodiscard]] dasall::tools::plugin::ToolPluginExtensionCatalog make_plugin_catalog() {
  return dasall::tools::plugin::ToolPluginExtensionCatalog{
      .payload_kinds = {dasall::tools::plugin::ToolPluginPayloadKind::mcp_server_stdio},
      .builtin_tool_providers = {},
      .mcp_stdio_servers = {
          dasall::tools::plugin::MCPServerStdioExport{
              .provider_ref = {
                  .plugin_id = std::string(kPluginId),
                  .export_key = std::string("mcp.echo"),
                  .source_revision = std::string("rev-a"),
              },
              .server_id = std::string(kServerId),
              .launch_spec_ref = std::string("launch://plugin.loopback/mcp.echo.v1"),
              .trust_level = std::string("trusted-local"),
          },
      },
      .skill_bundles = {},
  };
}

[[nodiscard]] dasall::tools::mcp::StdioMCPLaunchSample make_launch_sample() {
  return dasall::tools::mcp::StdioMCPLaunchSample{
      .launch_spec_ref = std::string("launch://plugin.loopback/mcp.echo.v1"),
      .command = std::string("tests/mocks/bin/mcp-loopback-server"),
      .args = {"--scenario", "plugin-echo"},
      .env = {{"MCP_PROTOCOL_VERSION", "2025-03-26"}},
      .working_dir = std::string("${workspaceFolder}"),
      .protocol_version = std::string("2025-03-26"),
      .declared_capabilities = {"tools"},
      .tool_bindings = {
          dasall::tools::mcp::StdioLaunchBindingTemplate{
              .internal_tool_name = std::string("tool.echo"),
              .remote_tool_name = std::string("remote.echo"),
              .remote_capability_id = std::string("cap.remote.echo"),
              .input_schema_ref = std::string("schema://tools/tool.echo/input/v1"),
          },
      },
      .healthcheck_mode = std::string("initialize_roundtrip"),
  };
}

[[nodiscard]] std::shared_ptr<dasall::tools::mcp::StdioMCPServerLauncher> make_launcher(
    const dasall::tests::mocks::MCPLoopbackServerFixture& loopback) {
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
          .channel_builder = loopback.build_channel_builder(),
      });
}

[[nodiscard]] dasall::tools::ToolManager make_manager(
    const std::shared_ptr<dasall::tools::registry::ToolRegistry>& registry,
    const std::shared_ptr<dasall::tools::execution::BuiltinExecutorLane>& builtin_lane,
    const std::shared_ptr<dasall::tools::mcp::MCPLane>& mcp_lane) {
  dasall::tools::manager::ToolManagerDependencies dependencies;
  dependencies.registry = registry;
  dependencies.build_manifest = make_manifest();
  dependencies.executor = [builtin_lane, mcp_lane](const auto& execution_request) {
    const dasall::tools::ToolExecutionContext execution_context{
        .invocation_context = execution_request.invocation_context,
        .lane_key = execution_request.route_decision.lane_key,
    };

    if (execution_request.route_decision.route == dasall::contracts::ToolIRRoute::MCPRemote) {
      return mcp_lane->execute(
          execution_request.tool_ir,
          execution_context,
          execution_request.route_decision.server_id);
    }

    return builtin_lane->execute(execution_request.tool_ir, execution_context);
  };
  return dasall::tools::ToolManager(std::move(dependencies));
}

struct Harness {
  std::int64_t now_ms = 6000;
  dasall::profiles::RuntimePolicySnapshot snapshot;
  dasall::tools::ToolInvocationContext context;
  std::shared_ptr<dasall::tools::registry::ToolRegistry> registry;
  std::shared_ptr<dasall::tools::execution::BuiltinExecutorLane> builtin_lane;
  std::shared_ptr<dasall::tools::mcp::CapabilityCache> cache;
  dasall::tests::mocks::MCPLoopbackServerFixture loopback;
  std::shared_ptr<dasall::tools::mcp::StdioMCPServerLauncher> launcher;
  std::shared_ptr<dasall::tools::mcp::MCPAdapter> adapter;
  std::shared_ptr<dasall::tools::mcp::CapabilityDiscovery> discovery;
    dasall::tools::bridge::ToolPluginExtensionConsumer consumer;
  std::shared_ptr<dasall::tools::mcp::MCPLane> mcp_lane;
  std::unique_ptr<dasall::tools::ToolManager> manager;

  explicit Harness(dasall::tests::mocks::MCPLoopbackScenario scenario)
      : snapshot(make_snapshot()),
        context(make_context(snapshot)),
        registry(std::make_shared<dasall::tools::registry::ToolRegistry>()),
        builtin_lane(std::make_shared<dasall::tools::execution::BuiltinExecutorLane>(
            dasall::tools::execution::BuiltinExecutorLaneDependencies{
                .registry = registry,
                .service_bridge = nullptr,
                .execution_service = nullptr,
                .data_service = nullptr,
                .now_ms = {},
            })),
        cache(std::make_shared<dasall::tools::mcp::CapabilityCache>(
            dasall::tools::mcp::CapabilityCacheOptions{
                .expire_after_ms = 180000,
                .stale_read_allowed = true,
                .now_ms = [this]() { return now_ms; },
            })),
        loopback(std::move(scenario)),
        launcher(make_launcher(loopback)),
        adapter(std::make_shared<dasall::tools::mcp::MCPAdapter>(
            dasall::tools::mcp::MCPAdapterDependencies{
                .transport_factory = launcher->build_transport_factory(),
                .now_ms = [this]() { return now_ms; },
                .session_ref_builder = {},
                .request_timeout = std::chrono::milliseconds(50),
            })),
        discovery(std::make_shared<dasall::tools::mcp::CapabilityDiscovery>(
            dasall::tools::mcp::CapabilityDiscoveryDependencies{
                .capability_cache = cache,
                .adapter = adapter,
                .registry = registry,
                .launcher = launcher,
                .now_ms = [this]() { return now_ms; },
                .refresh_interval_ms = 1000,
                .failure_backoff_ms = 250,
            })),
        consumer(dasall::tools::bridge::ToolPluginExtensionConsumerDependencies{
            .registry = registry,
            .discovery = discovery,
            .skill_registry = nullptr,
            .builtin_descriptor_resolver = {},
            .skill_bundle_importer = {},
        }) {
    assert_true(registry->register_builtin(make_descriptor()),
                "plugin stdio integration harness should register the builtin descriptor before discovery publishes bindings");

    mcp_lane = std::make_shared<dasall::tools::mcp::MCPLane>(
        dasall::tools::mcp::MCPLaneDependencies{
            .registry = registry,
            .adapter = adapter,
            .server_spec_resolver = [this](std::string_view server_id) {
              return discovery->resolve_server_spec(server_id);
            },
        });
    manager = std::make_unique<dasall::tools::ToolManager>(
        make_manager(registry, builtin_lane, mcp_lane));
  }

  std::string publish_plugin_and_refresh() {
        assert_true(consumer.on_plugin_loaded(make_plugin_catalog()),
                                "plugin stdio integration should accept the plugin extension catalog through the consumer path");
        assert_true(discovery->resolve_server_spec(kServerId).has_value(),
                                "plugin stdio integration should publish the resolved MCP server spec through capability discovery");

    const auto summary = discovery->refresh_once();
    assert_equal(1, static_cast<int>(summary.refreshed_server_ids.size()),
                 "plugin stdio integration should refresh the discovered MCP server before invocation");

    manager->set_capability_snapshot(cache->snapshot(kServerId));
        return std::string(kSourceKey);
  }
};

void test_plugin_stdio_path_invokes_mcp_tool_end_to_end() {
  Harness harness(
      dasall::tests::mocks::MCPLoopbackScenario{
          .connection_id = std::string("conn-plugin-stdio"),
          .frames = {
              {"\"method\":\"initialize\"",
               std::string("{\"jsonrpc\":\"2.0\",\"id\":\"initialize:loopback\",\"result\":{\"protocolVersion\":\"2025-03-26\"}}")},
              {"\"method\":\"notifications/initialized\"", std::nullopt},
              {"\"method\":\"tools/list\"",
               std::string("{\"jsonrpc\":\"2.0\",\"id\":\"tools/list:loopback\",\"result\":{\"tools\":[{\"name\":\"remote.echo\",\"capabilityId\":\"cap.remote.echo\"}]}}")},
              {"\"method\":\"tools/call\"",
               std::string("{\"jsonrpc\":\"2.0\",\"id\":\"tools/call:loopback\",\"result\":{\"payload\":\"plugin-stdio-ok\"}}")},
          },
      });

  static_cast<void>(harness.publish_plugin_and_refresh());
  const auto envelope = harness.manager->invoke(make_request(), harness.context);

  assert_true(envelope.tool_result.has_value() && envelope.tool_result->success.value_or(false),
              "plugin stdio integration should succeed on the MCP invoke path");
  assert_equal(std::string("plugin-stdio-ok"), envelope.tool_result->payload.value_or(""),
               "plugin stdio integration should surface the loopback MCP payload");
  assert_true(envelope.route_facts.has_value(),
              "plugin stdio integration should project route facts for the MCP path");
  assert_equal(std::string("mcp"), envelope.route_facts->route_kind.value_or(""),
               "plugin stdio integration should select the MCP route when fresh capabilities are available");
  assert_equal(std::string("route.mcp.selected"),
               envelope.route_facts->decision_reason.value_or(""),
               "plugin stdio integration should record the MCP route selection reason");
  assert_equal(std::string(kServerId), envelope.route_facts->server_id.value_or(""),
               "plugin stdio integration should preserve the selected server id in route facts");
  assert_equal(std::string("tests/mocks/bin/mcp-loopback-server"), harness.loopback.last_command(),
               "plugin stdio integration should use the launch sample command resolved from launch_spec_ref");
  assert_equal(std::string(kServerId), harness.loopback.last_server_id(),
               "plugin stdio integration should connect the discovered MCP server id");
  assert_equal(4, static_cast<int>(harness.loopback.written_messages().size()),
               "plugin stdio integration should drive initialize, initialized, list, and call frames in order");
  assert_true(contains_fragment(harness.loopback.written_messages().front(), "\"method\":\"initialize\""),
              "plugin stdio integration should open with initialize");
  assert_true(contains_fragment(harness.loopback.written_messages().back(), "\"method\":\"tools/call\""),
              "plugin stdio integration should end with tools/call");
  assert_true(harness.loopback.completed(),
              "plugin stdio integration should consume the full loopback transcript");
}

void test_plugin_unload_revokes_bindings_and_cache_entries() {
  Harness harness(
      dasall::tests::mocks::MCPLoopbackScenario{
          .connection_id = std::string("conn-plugin-unload"),
          .frames = {
              {"\"method\":\"initialize\"",
               std::string("{\"jsonrpc\":\"2.0\",\"id\":\"initialize:loopback\",\"result\":{\"protocolVersion\":\"2025-03-26\"}}")},
              {"\"method\":\"notifications/initialized\"", std::nullopt},
              {"\"method\":\"tools/list\"",
               std::string("{\"jsonrpc\":\"2.0\",\"id\":\"tools/list:loopback\",\"result\":{\"tools\":[{\"name\":\"remote.echo\",\"capabilityId\":\"cap.remote.echo\"}]}}")},
          },
      });

  const auto source_key = harness.publish_plugin_and_refresh();
  assert_equal(1, static_cast<int>(harness.registry->list_mcp_bindings("tool.echo").size()),
               "plugin unload integration should start from a published MCP binding set");
  assert_true(harness.cache->snapshot(kServerId).has_value(),
              "plugin unload integration should start from a populated capability cache entry");

    assert_true(harness.consumer.on_plugin_unloaded(kPluginId),
                            "plugin unload integration should revoke the active source through the consumer path");

  assert_true(!harness.discovery->resolve_server_spec(kServerId).has_value(),
              "plugin unload integration should revoke the resolved server spec from discovery state");
  assert_equal(0, static_cast<int>(harness.registry->list_mcp_bindings("tool.echo").size()),
               "plugin unload integration should revoke source-scoped MCP bindings from the registry");
  assert_true(!harness.cache->snapshot(kServerId).has_value(),
              "plugin unload integration should invalidate the capability cache entry for the revoked server");
    static_cast<void>(source_key);
}

}  // namespace

int main() {
  try {
    test_plugin_stdio_path_invokes_mcp_tool_end_to_end();
    test_plugin_unload_revokes_bindings_and_cache_entries();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}