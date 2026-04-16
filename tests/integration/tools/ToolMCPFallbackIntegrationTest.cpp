#include <algorithm>
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
#include "error/ResultCode.h"
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
      .request_id = std::string("req-tool-mcp-fallback"),
      .tool_call_id = std::string("call-tool-mcp-fallback"),
      .tool_name = std::string("tool.echo"),
      .invocation_kind = dasall::contracts::ToolInvocationKind::Action,
      .arguments_payload = std::string("{\"message\":\"fallback\"}"),
      .created_at = 5000,
      .goal_id = std::string("goal-tool-mcp-fallback"),
      .worker_task_id = std::string("worker-tool-mcp-fallback"),
      .runtime_budget = std::nullopt,
      .timeout_ms = 2500U,
      .idempotency_key = std::string("idem-tool-mcp-fallback"),
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

[[nodiscard]] dasall::profiles::RuntimePolicySnapshot make_snapshot(bool stale_read_allowed) {
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
          .stale_read_allowed = stale_read_allowed,
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
      .session_id = std::string("session-tool-mcp-fallback"),
      .profile_snapshot = &snapshot,
      .trace = {
          .trace_id = std::string("trace-tool-mcp-fallback"),
          .span_id = std::string("span-tool-mcp-fallback"),
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::nullopt,
  };
}

[[nodiscard]] dasall::tools::bridge::MCPServerLaunchSpec make_launch_spec() {
  return dasall::tools::bridge::MCPServerLaunchSpec{
      .provider_ref = {
          .plugin_id = std::string("plugin.loopback"),
          .export_key = std::string("mcp.echo"),
          .source_revision = std::string("rev-a"),
      },
      .source_key = std::string(kSourceKey),
      .server_id = std::string(kServerId),
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
  std::int64_t now_ms = 5000;
  dasall::profiles::RuntimePolicySnapshot snapshot;
  dasall::tools::ToolInvocationContext context;
  std::shared_ptr<dasall::tools::registry::ToolRegistry> registry;
  std::shared_ptr<dasall::tools::execution::BuiltinExecutorLane> builtin_lane;
  std::shared_ptr<dasall::tools::mcp::CapabilityCache> cache;
  dasall::tests::mocks::MCPLoopbackServerFixture loopback;
  std::shared_ptr<dasall::tools::mcp::StdioMCPServerLauncher> launcher;
  std::shared_ptr<dasall::tools::mcp::MCPAdapter> adapter;
  std::shared_ptr<dasall::tools::mcp::CapabilityDiscovery> discovery;
  std::shared_ptr<dasall::tools::mcp::MCPLane> mcp_lane;
  std::unique_ptr<dasall::tools::ToolManager> manager;

  explicit Harness(dasall::tests::mocks::MCPLoopbackScenario scenario,
                   bool stale_read_allowed = true)
      : snapshot(make_snapshot(stale_read_allowed)),
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
                .stale_read_allowed = stale_read_allowed,
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
            })) {
    assert_true(registry->register_builtin(make_descriptor()),
                "fallback integration harness should register the builtin descriptor before publishing MCP bindings");

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

  void prime_discovery() {
    assert_true(discovery->on_plugin_delta(std::string(kSourceKey), {make_launch_spec()}),
                "fallback integration should accept the direct launch spec delta");
    const auto summary = discovery->refresh_once();
    assert_equal(1, static_cast<int>(summary.refreshed_server_ids.size()),
                 "fallback integration should refresh the MCP server once before route testing");
  }

  void sync_capability_snapshot() {
    manager->set_capability_snapshot(cache->snapshot(kServerId));
  }
};

void test_stale_trusted_snapshot_prefers_mcp_route() {
  Harness harness(
      dasall::tests::mocks::MCPLoopbackScenario{
          .connection_id = std::string("conn-stale-fallback"),
          .frames = {
              {"\"method\":\"initialize\"",
               std::string("{\"jsonrpc\":\"2.0\",\"id\":\"initialize:loopback\",\"result\":{\"protocolVersion\":\"2025-03-26\"}}")},
              {"\"method\":\"notifications/initialized\"", std::nullopt},
              {"\"method\":\"tools/list\"",
               std::string("{\"jsonrpc\":\"2.0\",\"id\":\"tools/list:loopback\",\"result\":{\"tools\":[{\"name\":\"remote.echo\",\"capabilityId\":\"cap.remote.echo\"}]}}")},
              {"\"method\":\"tools/call\"",
               std::string("{\"jsonrpc\":\"2.0\",\"id\":\"tools/call:loopback\",\"result\":{\"payload\":\"stale-ok\"}}")},
          },
      });

  harness.prime_discovery();
  harness.cache->mark_failed(std::string(kServerId), "mcp.discovery.timeout");
  const auto stale_snapshot = harness.cache->snapshot(kServerId);
  assert_true(stale_snapshot.has_value() &&
                  stale_snapshot->freshness == dasall::tools::CapabilityFreshness::stale &&
                  stale_snapshot->trust_marker.has_value(),
              "stale-route integration should hold a trusted stale snapshot before invoking ToolManager");

  harness.sync_capability_snapshot();
  const auto envelope = harness.manager->invoke(make_request(), harness.context);

  assert_true(envelope.tool_result.has_value() && envelope.tool_result->success.value_or(false),
              "stale-route integration should still succeed through the MCP lane");
  assert_equal(std::string("stale-ok"), envelope.tool_result->payload.value_or(""),
               "stale-route integration should surface the MCP payload without falling back");
  assert_true(envelope.route_facts.has_value(),
              "stale-route integration should emit route facts for the selected MCP lane");
  assert_equal(std::string("mcp"), envelope.route_facts->route_kind.value_or(""),
               "stale-route integration should keep the MCP route selected for trusted stale snapshots");
  assert_equal(std::string("route.mcp.stale_fallback"),
               envelope.route_facts->decision_reason.value_or(""),
               "stale-route integration should explain the stale fallback decision explicitly");
  assert_equal(std::string(kServerId), envelope.route_facts->server_id.value_or(""),
               "stale-route integration should preserve the selected MCP server id");
  assert_true(!envelope.failure_reason_code.has_value(),
              "stale-route integration should not surface a failure reason on the successful MCP path");
  assert_true(harness.loopback.completed(),
              "stale-route integration should consume the full MCP loopback transcript");
}

void test_builtin_fallback_is_used_when_mcp_lane_is_unhealthy() {
  Harness harness(
      dasall::tests::mocks::MCPLoopbackScenario{
          .connection_id = std::string("conn-builtin-fallback"),
          .frames = {
              {"\"method\":\"initialize\"",
               std::string("{\"jsonrpc\":\"2.0\",\"id\":\"initialize:loopback\",\"result\":{\"protocolVersion\":\"2025-03-26\"}}")},
              {"\"method\":\"notifications/initialized\"", std::nullopt},
              {"\"method\":\"tools/list\"",
               std::string("{\"jsonrpc\":\"2.0\",\"id\":\"tools/list:loopback\",\"result\":{\"tools\":[{\"name\":\"remote.echo\",\"capabilityId\":\"cap.remote.echo\"}]}}")},
          },
      });

  harness.prime_discovery();
  harness.cache->mark_failed(std::string(kServerId), "mcp.discovery.timeout");
  harness.sync_capability_snapshot();
  harness.manager->set_route_health(dasall::tools::route::ToolRouteHealthSnapshot{
      .builtin_lane_healthy = true,
      .workflow_lane_healthy = true,
      .mcp_lane_healthy = false,
  });

  const auto envelope = harness.manager->invoke(make_request(), harness.context);

  assert_true(envelope.tool_result.has_value() && envelope.tool_result->success.value_or(false),
              "builtin fallback integration should still succeed when the MCP lane is unhealthy");
  assert_true(envelope.tool_result->payload.has_value() &&
                  contains_fragment(*envelope.tool_result->payload, "\"status\":\"executed\""),
              "builtin fallback integration should return the builtin execution payload");
  assert_true(envelope.route_facts.has_value(),
              "builtin fallback integration should emit route facts for the fallback lane");
  assert_equal(std::string("builtin"), envelope.route_facts->route_kind.value_or(""),
               "builtin fallback integration should switch to the builtin route when MCP is unhealthy");
  assert_equal(std::string("route.builtin.selected"),
               envelope.route_facts->decision_reason.value_or(""),
               "builtin fallback integration should record the builtin route decision");
  assert_equal(3, static_cast<int>(harness.loopback.written_messages().size()),
               "builtin fallback integration should stop after discovery and avoid a remote tools/call");
}

void test_mcp_protocol_error_mapping_is_preserved_through_tool_manager() {
  Harness harness(
      dasall::tests::mocks::MCPLoopbackScenario{
          .connection_id = std::string("conn-mcp-error"),
          .frames = {
              {"\"method\":\"initialize\"",
               std::string("{\"jsonrpc\":\"2.0\",\"id\":\"initialize:loopback\",\"result\":{\"protocolVersion\":\"2025-03-26\"}}")},
              {"\"method\":\"notifications/initialized\"", std::nullopt},
              {"\"method\":\"tools/list\"",
               std::string("{\"jsonrpc\":\"2.0\",\"id\":\"tools/list:loopback\",\"result\":{\"tools\":[{\"name\":\"remote.echo\",\"capabilityId\":\"cap.remote.echo\"}]}}")},
              {"\"method\":\"tools/call\"",
               std::string("{\"jsonrpc\":\"2.0\",\"id\":\"tools/call:loopback\",\"error\":{\"message\":\"loopback.failure\"}}")},
          },
      });

  harness.prime_discovery();
  harness.sync_capability_snapshot();

  const auto envelope = harness.manager->invoke(make_request(), harness.context);

  assert_true(envelope.tool_result.has_value() && !envelope.tool_result->success.value_or(true),
              "MCP error integration should keep the tool result in a failed state");
  assert_true(envelope.tool_result->error.has_value(),
              "MCP error integration should surface ErrorInfo through ToolManager projection");
  assert_equal(std::string("mcp"), envelope.route_facts->route_kind.value_or(""),
               "MCP error integration should still record the MCP route facts on failure");
  assert_equal(std::string("mcp.protocol_error:loopback.failure"),
               envelope.failure_reason_code.value_or(""),
               "MCP error integration should preserve the normalized protocol error code");
  assert_equal(std::string("mcp.protocol_error:loopback.failure"),
               envelope.tool_result->error->details.message,
               "MCP error integration should keep the ErrorInfo message aligned with the failure reason");
  assert_equal(std::string("tools.mcp.invoke"), envelope.tool_result->error->details.stage,
               "MCP error integration should preserve the MCP invoke stage in ErrorInfo");
  assert_equal(std::string("mcp.adapter"), envelope.tool_result->error->source_ref.ref_type,
               "MCP error integration should attribute protocol failures to the MCP adapter");
  assert_true(envelope.tool_result->error->details.code.has_value() &&
                  *envelope.tool_result->error->details.code ==
                      static_cast<int>(dasall::contracts::ResultCode::ToolExecutionFailed),
              "MCP error integration should keep the ToolExecutionFailed result code for protocol failures");
}

}  // namespace

int main() {
  try {
    test_stale_trusted_snapshot_prefers_mcp_route();
    test_builtin_fallback_is_used_when_mcp_lane_is_unhealthy();
    test_mcp_protocol_error_mapping_is_preserved_through_tool_manager();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}