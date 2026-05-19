#include <condition_variable>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ToolManager.h"
#include "registry/ToolRegistry.h"
#include "support/TestAssertions.h"
#include "support/ToolsIntegrationFixture.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tests::support::make_action_request;
using dasall::tests::support::make_builtin_action_descriptor;
using dasall::tests::support::make_integration_context;
using dasall::tests::support::make_integration_snapshot;
using dasall::tests::support::ToolsSnapshotOverrides;

struct BlockingExecutorState {
  std::mutex mutex;
  std::condition_variable entered_cv;
  std::condition_variable release_cv;
  bool builtin_hold_entered = false;
  bool mcp_hold_entered = false;
  bool release_builtin_hold = false;
  bool release_mcp_hold = false;
};

[[nodiscard]] dasall::contracts::ToolRequest make_mcp_request(
    const std::string& suffix,
    const std::string& tool_name = "tool.echo") {
  auto request = make_action_request(tool_name, suffix);
  request.tags = std::vector<std::string>{"integration", "tools", "tool.route.mcp"};
  return request;
}

[[nodiscard]] dasall::tools::CapabilitySnapshot make_mcp_snapshot() {
  return dasall::tools::CapabilitySnapshot{
      .server_id = "mcp.echo",
      .entries = {{
          .capability_id = "cap.echo",
          .capability_version = "1.0",
          .tool_names = {"remote.echo"},
      }},
      .freshness = dasall::tools::CapabilityFreshness::fresh,
      .last_refresh_at_ms = 1000,
      .last_error = std::nullopt,
      .trust_marker = std::string("trusted"),
  };
}

[[nodiscard]] dasall::contracts::ToolResult make_success_result(
    const dasall::tools::manager::ToolExecutionRequest& execution_request,
    std::string payload,
    std::vector<std::string> tags) {
  return dasall::contracts::ToolResult{
      .request_id = execution_request.tool_ir.request_id,
      .tool_call_id = execution_request.tool_ir.tool_call_id,
      .tool_name = execution_request.tool_ir.tool_name,
      .success = true,
      .payload = std::move(payload),
      .error = std::nullopt,
      .side_effects = std::vector<std::string>{"side_effect:none"},
      .completed_at = 2000,
      .duration_ms = 25,
      .goal_id = execution_request.tool_ir.goal_id,
      .worker_task_id = execution_request.tool_ir.worker_task_id,
      .tags = std::move(tags),
  };
}

[[nodiscard]] dasall::tools::ToolManager make_manager(
    const std::shared_ptr<BlockingExecutorState>& state) {
  auto registry = std::make_shared<dasall::tools::registry::ToolRegistry>();
  assert_true(registry->register_builtin(make_builtin_action_descriptor("agent.terminal")),
              "lane backpressure integration test should register builtin descriptor");
  assert_true(registry->register_builtin(make_builtin_action_descriptor("tool.echo")),
              "lane backpressure integration test should register MCP-backed descriptor");
  assert_true(
      registry->upsert_mcp_bindings(
          "integration.mcp",
          std::vector<dasall::tools::mcp::MCPToolBinding>{dasall::tools::mcp::MCPToolBinding{
              .internal_tool_name = "tool.echo",
              .remote_tool_name = "remote.echo",
              .server_id = "mcp.echo",
              .remote_capability_id = std::string("cap.echo"),
              .input_schema_ref = std::string("schema://tools/tool.echo/input/v1"),
          }}),
      "lane backpressure integration test should register MCP binding");

  auto lane_pool = std::make_shared<dasall::tools::execution::ExecutorLanePool>();

  dasall::tools::manager::ToolManagerDependencies dependencies;
  dependencies.registry = registry;
  dependencies.lane_pool = lane_pool;
  dependencies.executor = [state](const dasall::tools::manager::ToolExecutionRequest& execution_request) {
    if (execution_request.route_decision.route == dasall::contracts::ToolIRRoute::LocalTool &&
        execution_request.tool_ir.tool_call_id == std::string("call-builtin-hold")) {
      std::unique_lock<std::mutex> lock(state->mutex);
      state->builtin_hold_entered = true;
      state->entered_cv.notify_all();
      state->release_cv.wait(lock, [&]() { return state->release_builtin_hold; });
    }

    if (execution_request.route_decision.route == dasall::contracts::ToolIRRoute::MCPRemote &&
        execution_request.tool_ir.tool_call_id == std::string("call-mcp-hold")) {
      std::unique_lock<std::mutex> lock(state->mutex);
      state->mcp_hold_entered = true;
      state->entered_cv.notify_all();
      state->release_cv.wait(lock, [&]() { return state->release_mcp_hold; });
    }

    if (execution_request.route_decision.route == dasall::contracts::ToolIRRoute::MCPRemote) {
      return make_success_result(
          execution_request,
          std::string("{\"source\":\"mcp\"}"),
          std::vector<std::string>{"integration", "mcp", execution_request.route_decision.lane_key});
    }

    return make_success_result(
        execution_request,
        std::string("{\"source\":\"builtin\"}"),
        std::vector<std::string>{"integration", "builtin", execution_request.route_decision.lane_key});
  };

  dasall::tools::ToolManager manager(std::move(dependencies));
  manager.set_capability_snapshot(make_mcp_snapshot());
  return manager;
}

void test_builtin_lane_backpressure_surfaces_stable_reason_code() {
  auto state = std::make_shared<BlockingExecutorState>();
  auto manager = make_manager(state);
  const auto snapshot = make_integration_snapshot(ToolsSnapshotOverrides{
      .max_tool_calls = 1U,
      .allowed_tool_domains = {"builtin", "mcp"},
      .tool_visibility_rules = {"builtin:all", "mcp:all"},
  });
  const auto context = make_integration_context(&snapshot, "builtin-backpressure");

  dasall::tools::ToolInvocationEnvelope held_envelope;
  std::thread held_request([&]() {
    held_envelope = manager.invoke(make_action_request("agent.terminal", "builtin-hold"), context);
  });

  {
    std::unique_lock<std::mutex> lock(state->mutex);
    state->entered_cv.wait(lock, [&]() { return state->builtin_hold_entered; });
  }

  const auto overflow_envelope =
      manager.invoke(make_action_request("agent.terminal", "builtin-overflow"), context);
  assert_true(overflow_envelope.tool_result.has_value() &&
                  !overflow_envelope.tool_result->success.value_or(true),
              "second builtin invoke should fail while the first builtin call holds the only permit");
  assert_true(overflow_envelope.failure_reason_code.has_value(),
              "builtin lane overflow should expose a stable failure reason");
  assert_equal(std::string("lane.builtin.backpressure"),
               *overflow_envelope.failure_reason_code,
               "builtin lane overflow should propagate the builtin backpressure reason code");

  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->release_builtin_hold = true;
  }
  state->release_cv.notify_all();
  held_request.join();

  assert_true(held_envelope.tool_result.has_value() && held_envelope.tool_result->success.value_or(false),
              "held builtin request should complete successfully once released");
}

void test_mcp_lane_saturation_does_not_block_builtin_lane() {
  auto state = std::make_shared<BlockingExecutorState>();
  auto manager = make_manager(state);
  const auto snapshot = make_integration_snapshot(ToolsSnapshotOverrides{
      .max_tool_calls = 1U,
      .allowed_tool_domains = {"builtin", "mcp"},
      .tool_visibility_rules = {"builtin:all", "mcp:all"},
  });
  const auto context = make_integration_context(&snapshot, "mcp-bulkhead");

  dasall::tools::ToolInvocationEnvelope held_envelope;
  std::thread held_request([&]() {
    held_envelope = manager.invoke(make_mcp_request("mcp-hold"), context);
  });

  {
    std::unique_lock<std::mutex> lock(state->mutex);
    state->entered_cv.wait(lock, [&]() { return state->mcp_hold_entered; });
  }

  const auto mcp_overflow = manager.invoke(make_mcp_request("mcp-overflow"), context);
  assert_true(mcp_overflow.tool_result.has_value() && !mcp_overflow.tool_result->success.value_or(true),
              "second MCP invoke should fail while the first MCP call holds the only server-scoped permit");
  assert_true(mcp_overflow.failure_reason_code.has_value(),
              "MCP lane overflow should surface a stable failure reason");
  assert_equal(std::string("lane.mcp.backpressure"), *mcp_overflow.failure_reason_code,
               "MCP lane overflow should propagate the MCP backpressure reason code");

  const auto builtin_envelope =
      manager.invoke(make_action_request("agent.terminal", "builtin-after-mcp"), context);
  assert_true(builtin_envelope.tool_result.has_value() && builtin_envelope.tool_result->success.value_or(false),
              "builtin invoke should continue succeeding while the MCP lane is saturated");

  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->release_mcp_hold = true;
  }
  state->release_cv.notify_all();
  held_request.join();

  assert_true(held_envelope.tool_result.has_value() && held_envelope.tool_result->success.value_or(false),
              "held MCP request should complete successfully once released");
}

}  // namespace

int main() {
  try {
    test_builtin_lane_backpressure_surfaces_stable_reason_code();
    test_mcp_lane_saturation_does_not_block_builtin_lane();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}