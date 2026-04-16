#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "config/ToolConfigAdapter.h"
#include "route/ToolRouteSelector.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::tools::config::ToolTimeoutView make_timeout_view(
    bool stale_read_allowed,
    bool builtin_enabled = true,
    bool mcp_enabled = true) {
  return dasall::tools::config::ToolTimeoutView{
      .tool = {.timeout_ms = 2500, .retry_budget = 1U, .circuit_breaker_threshold = 4U},
      .mcp = {.timeout_ms = 2000, .retry_budget = 1U, .circuit_breaker_threshold = 3U},
      .workflow = {.timeout_ms = 5000, .retry_budget = 1U, .circuit_breaker_threshold = 3U},
      .max_tool_calls = 24U,
      .builtin_lane_enabled = builtin_enabled,
      .mcp_lane_enabled = mcp_enabled,
      .multi_agent_enabled = true,
      .capability_refresh_interval_ms = 30000,
      .capability_expire_after_ms = 180000,
      .stale_read_allowed = stale_read_allowed,
      .capability_failure_backoff_ms = 5000,
      .snapshot_fingerprint = 21U,
  };
}

[[nodiscard]] dasall::contracts::ToolDescriptor make_action_descriptor() {
  return dasall::contracts::ToolDescriptor{
      .tool_name = std::string("tool.echo"),
      .display_name = std::string("Echo"),
      .category = dasall::contracts::ToolCategory::Action,
      .capability_tier = dasall::contracts::ToolCapabilityTier::Stable,
      .is_read_only = false,
      .supports_compensation = false,
      .default_timeout_ms = 2500U,
      .input_schema_ref = std::string("schema://input"),
      .output_schema_ref = std::string("schema://output"),
      .required_scopes = std::vector<std::string>{"trusted"},
      .tags = std::vector<std::string>{"tool.route.mcp"},
      .version = std::string("1.0.0"),
  };
}

[[nodiscard]] dasall::contracts::ToolIR make_mcp_tool_ir() {
  return dasall::contracts::ToolIR{
      .request_id = std::string("req-1"),
      .tool_call_id = std::string("call-1"),
      .tool_name = std::string("tool.echo"),
      .operation = dasall::contracts::ToolIROperation::Invoke,
      .normalized_arguments = std::string("{}"),
      .route = dasall::contracts::ToolIRRoute::MCPRemote,
      .timeout_ms = 2500U,
      .idempotency_key = std::string("idem-1"),
      .priority = dasall::contracts::ToolIRPriority::Normal,
      .goal_id = std::string("goal-1"),
      .worker_task_id = std::string("worker-1"),
  };
}

[[nodiscard]] std::vector<dasall::tools::mcp::MCPToolBinding> make_bindings() {
  return {dasall::tools::mcp::MCPToolBinding{
      .internal_tool_name = "tool.echo",
      .remote_tool_name = "remote.echo",
      .server_id = "mcp.echo",
      .remote_capability_id = std::string("cap.echo"),
      .input_schema_ref = std::string("schema://input"),
  }};
}

[[nodiscard]] dasall::tools::CapabilitySnapshot make_snapshot(
    dasall::tools::CapabilityFreshness freshness,
    std::optional<std::string> trust_marker = std::string("trusted")) {
  return dasall::tools::CapabilitySnapshot{
      .server_id = "mcp.echo",
      .entries = {{
          .capability_id = "cap.echo",
          .capability_version = "1.0",
          .tool_names = {"remote.echo"},
      }},
      .freshness = freshness,
      .last_refresh_at_ms = 1000,
      .last_error = std::nullopt,
      .trust_marker = std::move(trust_marker),
  };
}

void test_stale_snapshot_can_fallback_to_mcp_when_profile_allows_it() {
  dasall::tools::route::ToolRouteSelector selector;
  const auto decision = selector.select_route(
      make_mcp_tool_ir(),
      make_action_descriptor(),
      make_timeout_view(true, false, true),
      make_bindings(),
      make_snapshot(dasall::tools::CapabilityFreshness::stale),
      dasall::tools::route::ToolRouteHealthSnapshot{
          .builtin_lane_healthy = false,
          .workflow_lane_healthy = true,
          .mcp_lane_healthy = true,
      });

  assert_true(decision.available, "stale snapshot should be usable when the profile allows stale reads");
  assert_true(decision.uses_stale_snapshot,
              "stale fallback should set the stale snapshot marker");
  assert_equal(std::string("route.mcp.stale_fallback"), decision.reason_code,
               "stale fallback should carry the stale-fallback reason");
}

void test_route_unavailable_when_no_lane_can_be_selected() {
  dasall::tools::route::ToolRouteSelector selector;
  const auto decision = selector.select_route(
      make_mcp_tool_ir(),
      make_action_descriptor(),
      make_timeout_view(false, false, false),
      {},
      std::nullopt,
      dasall::tools::route::ToolRouteHealthSnapshot{
          .builtin_lane_healthy = false,
          .workflow_lane_healthy = false,
          .mcp_lane_healthy = false,
      });

  assert_true(!decision.available, "route selector should fail closed when no lane is usable");
  assert_equal(std::string("RouteUnavailable"), decision.reason_code,
               "route selector should emit the RouteUnavailable reason");
}

}  // namespace

int main() {
  try {
    test_stale_snapshot_can_fallback_to_mcp_when_profile_allows_it();
    test_route_unavailable_when_no_lane_can_be_selected();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}