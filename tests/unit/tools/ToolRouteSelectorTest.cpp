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

[[nodiscard]] dasall::tools::config::ToolTimeoutView make_timeout_view() {
  return dasall::tools::config::ToolTimeoutView{
      .tool = {.timeout_ms = 2500, .retry_budget = 1U, .circuit_breaker_threshold = 4U},
      .mcp = {.timeout_ms = 2000, .retry_budget = 1U, .circuit_breaker_threshold = 3U},
      .workflow = {.timeout_ms = 5000, .retry_budget = 1U, .circuit_breaker_threshold = 3U},
      .max_tool_calls = 24U,
      .builtin_lane_enabled = true,
      .mcp_lane_enabled = true,
      .multi_agent_enabled = true,
      .capability_refresh_interval_ms = 30000,
      .capability_expire_after_ms = 180000,
      .stale_read_allowed = false,
      .capability_failure_backoff_ms = 5000,
      .snapshot_fingerprint = 11U,
  };
}

[[nodiscard]] dasall::contracts::ToolDescriptor make_descriptor(
    dasall::contracts::ToolCategory category,
    bool read_only = false) {
  return dasall::contracts::ToolDescriptor{
      .tool_name = std::string("tool.echo"),
      .display_name = std::string("Echo"),
      .category = category,
      .capability_tier = dasall::contracts::ToolCapabilityTier::Stable,
      .is_read_only = read_only,
      .supports_compensation = false,
      .default_timeout_ms = 2500U,
      .input_schema_ref = std::string("schema://input"),
      .output_schema_ref = std::string("schema://output"),
      .required_scopes = std::vector<std::string>{"trusted"},
      .tags = std::vector<std::string>{"tool.route.local"},
      .version = std::string("1.0.0"),
  };
}

[[nodiscard]] dasall::contracts::ToolIR make_tool_ir(dasall::contracts::ToolIRRoute route_hint) {
  return dasall::contracts::ToolIR{
      .request_id = std::string("req-1"),
      .tool_call_id = std::string("call-1"),
      .tool_name = std::string("tool.echo"),
      .operation = dasall::contracts::ToolIROperation::Invoke,
      .normalized_arguments = std::string("{}"),
      .route = route_hint,
      .timeout_ms = 2500U,
      .idempotency_key = std::string("idem-1"),
      .priority = dasall::contracts::ToolIRPriority::Normal,
      .goal_id = std::string("goal-1"),
      .worker_task_id = std::string("worker-1"),
  };
}

[[nodiscard]] dasall::tools::CapabilitySnapshot make_capability_snapshot(
    dasall::tools::CapabilityFreshness freshness) {
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
      .trust_marker = std::string("trusted"),
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

void test_workflow_builtin_and_mcp_routes_are_selectable() {
  dasall::tools::route::ToolRouteSelector selector;
  const auto timeout_view = make_timeout_view();
  const auto health = dasall::tools::route::ToolRouteHealthSnapshot{};

  const auto workflow_decision = selector.select_route(
      make_tool_ir(dasall::contracts::ToolIRRoute::WorkflowEngine),
      make_descriptor(dasall::contracts::ToolCategory::Workflow),
      timeout_view,
      {},
      std::nullopt,
      health);
  const auto builtin_decision = selector.select_route(
      make_tool_ir(dasall::contracts::ToolIRRoute::LocalTool),
      make_descriptor(dasall::contracts::ToolCategory::Action, true),
      timeout_view,
      {},
      std::nullopt,
      health);
  const auto mcp_decision = selector.select_route(
      make_tool_ir(dasall::contracts::ToolIRRoute::MCPRemote),
      make_descriptor(dasall::contracts::ToolCategory::Action),
      timeout_view,
      make_bindings(),
      make_capability_snapshot(dasall::tools::CapabilityFreshness::fresh),
      health);

  assert_true(workflow_decision.available, "workflow descriptor should route to workflow lane");
  assert_equal(std::string("route.workflow.selected"), workflow_decision.reason_code,
               "workflow route should carry the workflow-selected reason");
  assert_equal(std::string("workflow"), workflow_decision.lane_key,
               "workflow route should reserve the workflow lane");

  assert_true(builtin_decision.available, "builtin action should stay on the builtin lane");
  assert_equal(std::string("route.builtin.selected"), builtin_decision.reason_code,
               "builtin route should carry the builtin-selected reason");
  assert_equal(std::string("builtin"), builtin_decision.lane_key,
               "builtin route should reserve the builtin lane");

  assert_true(mcp_decision.available, "fresh mcp capability should be routable");
  assert_equal(std::string("route.mcp.selected"), mcp_decision.reason_code,
               "fresh mcp route should carry the mcp-selected reason");
  assert_equal(std::string("mcp:mcp.echo"), mcp_decision.lane_key,
               "mcp route should reserve the server-scoped mcp lane");
}

}  // namespace

int main() {
  try {
    test_workflow_builtin_and_mcp_routes_are_selectable();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}