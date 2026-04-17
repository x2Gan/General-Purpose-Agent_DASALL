#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "ToolManager.h"
#include "execution/BuiltinExecutorLane.h"
#include "registry/ToolRegistry.h"
#include "support/TestAssertions.h"
#include "support/ToolsIntegrationFixture.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tests::support::make_builtin_action_descriptor;
using dasall::tests::support::make_builtin_query_descriptor;
using dasall::tests::support::make_integration_context;
using dasall::tests::support::make_integration_snapshot;

[[nodiscard]] dasall::tools::ToolManager make_batch_manager() {
  auto registry = std::make_shared<dasall::tools::registry::ToolRegistry>();
  assert_true(registry->register_builtin(make_builtin_action_descriptor("agent.terminal")),
              "batch independence test should register builtin action descriptor");
  assert_true(registry->register_builtin(make_builtin_query_descriptor("agent.dataset")),
              "batch independence test should register builtin query descriptor");

  auto builtin_lane = std::make_shared<dasall::tools::execution::BuiltinExecutorLane>(
      dasall::tools::execution::BuiltinExecutorLaneDependencies{
          .registry = registry,
          .service_bridge = nullptr,
          .execution_service = nullptr,
          .data_service = nullptr,
          .now_ms = {},
      });

  dasall::tools::manager::ToolManagerDependencies dependencies;
  dependencies.registry = registry;
  dependencies.executor = [builtin_lane](const auto& execution_request) {
    return builtin_lane->execute(
        execution_request.tool_ir,
        dasall::tools::ToolExecutionContext{
            .invocation_context = execution_request.invocation_context,
            .lane_key = execution_request.route_decision.lane_key,
        });
  };
  return dasall::tools::ToolManager(std::move(dependencies));
}

void test_batch_invoke_preserves_per_request_independence() {
  auto manager = make_batch_manager();
  const auto snapshot = make_integration_snapshot();
  const auto context = make_integration_context(&snapshot, "batch");

  const std::vector<dasall::contracts::ToolRequest> requests{
      dasall::contracts::ToolRequest{
          .request_id = std::string("req-batch-action"),
          .tool_call_id = std::string("call-batch-action"),
          .tool_name = std::string("agent.terminal"),
          .invocation_kind = dasall::contracts::ToolInvocationKind::Action,
          .arguments_payload = std::string("{\"command\":\"batch-echo\"}"),
          .created_at = 1000,
          .goal_id = std::string("goal-batch-action"),
          .worker_task_id = std::string("worker-batch-action"),
          .runtime_budget = std::nullopt,
          .timeout_ms = 2500U,
          .idempotency_key = std::string("idem-batch-action"),
          .tags = std::vector<std::string>{"batch", "action"},
      },
      dasall::contracts::ToolRequest{
          .request_id = std::string("req-batch-query"),
          .tool_call_id = std::string("call-batch-query"),
          .tool_name = std::string("agent.dataset"),
          .invocation_kind = dasall::contracts::ToolInvocationKind::InformationQuery,
          .arguments_payload = std::string("{\"scope\":\"batch\"}"),
          .created_at = 1001,
          .goal_id = std::string("goal-batch-query"),
          .worker_task_id = std::string("worker-batch-query"),
          .runtime_budget = std::nullopt,
          .timeout_ms = 2500U,
          .idempotency_key = std::string("idem-batch-query"),
          .tags = std::vector<std::string>{"batch", "query"},
      },
      dasall::contracts::ToolRequest{
          .request_id = std::string("req-batch-missing"),
          .tool_call_id = std::string("call-batch-missing"),
          .tool_name = std::string("tool.missing"),
          .invocation_kind = dasall::contracts::ToolInvocationKind::Action,
          .arguments_payload = std::string("{}"),
          .created_at = 1002,
          .goal_id = std::string("goal-batch-missing"),
          .worker_task_id = std::string("worker-batch-missing"),
          .runtime_budget = std::nullopt,
          .timeout_ms = 2500U,
          .idempotency_key = std::string("idem-batch-missing"),
          .tags = std::vector<std::string>{"batch", "negative"},
      },
  };

  const auto envelopes = manager.invoke_batch(
      std::span<const dasall::contracts::ToolRequest>(requests), context);

  assert_equal(static_cast<std::size_t>(3U), envelopes.size(),
               "invoke_batch should return one envelope per request");

  assert_true(envelopes[0].tool_result.has_value() &&
                  envelopes[0].tool_result->success.value_or(false),
              "invoke_batch request[0] (action) should succeed independently");
  assert_true(envelopes[0].tool_result->tool_name.has_value() &&
                  *envelopes[0].tool_result->tool_name == "agent.terminal",
              "invoke_batch request[0] should preserve its own tool_name");

  assert_true(envelopes[1].tool_result.has_value() &&
                  envelopes[1].tool_result->success.value_or(false),
              "invoke_batch request[1] (query) should succeed independently");
  assert_true(envelopes[1].tool_result->tool_name.has_value() &&
                  *envelopes[1].tool_result->tool_name == "agent.dataset",
              "invoke_batch request[1] should preserve its own tool_name");

  assert_true(envelopes[2].tool_result.has_value() &&
                  !envelopes[2].tool_result->success.value_or(true),
              "invoke_batch request[2] (missing) should fail closed without affecting prior requests");
  assert_true(envelopes[2].failure_reason_code.has_value() &&
                  envelopes[2].failure_reason_code->find("descriptor_missing") != std::string::npos,
              "invoke_batch request[2] should surface descriptor_missing reason");

  assert_true(envelopes[0].tool_result->request_id != envelopes[1].tool_result->request_id,
              "invoke_batch should preserve per-request identity across envelopes");
  assert_true(envelopes[0].tool_result->tool_call_id != envelopes[2].tool_result->tool_call_id,
              "invoke_batch should preserve per-request tool_call_id independence");
}

void test_batch_invoke_single_failure_does_not_pollute_others() {
  auto manager = make_batch_manager();
  const auto snapshot = make_integration_snapshot();
  const auto context = make_integration_context(&snapshot, "batch-isolation");

  const std::vector<dasall::contracts::ToolRequest> requests{
      dasall::contracts::ToolRequest{
          .request_id = std::string("req-iso-missing"),
          .tool_call_id = std::string("call-iso-missing"),
          .tool_name = std::string("tool.missing"),
          .invocation_kind = dasall::contracts::ToolInvocationKind::Action,
          .arguments_payload = std::string("{}"),
          .created_at = 1000,
          .goal_id = std::string("goal-iso-missing"),
          .worker_task_id = std::string("worker-iso-missing"),
          .runtime_budget = std::nullopt,
          .timeout_ms = 2500U,
          .idempotency_key = std::string("idem-iso-missing"),
          .tags = std::vector<std::string>{"batch", "negative"},
      },
      dasall::contracts::ToolRequest{
          .request_id = std::string("req-iso-action"),
          .tool_call_id = std::string("call-iso-action"),
          .tool_name = std::string("agent.terminal"),
          .invocation_kind = dasall::contracts::ToolInvocationKind::Action,
          .arguments_payload = std::string("{\"command\":\"iso-echo\"}"),
          .created_at = 1001,
          .goal_id = std::string("goal-iso-action"),
          .worker_task_id = std::string("worker-iso-action"),
          .runtime_budget = std::nullopt,
          .timeout_ms = 2500U,
          .idempotency_key = std::string("idem-iso-action"),
          .tags = std::vector<std::string>{"batch", "action"},
      },
  };

  const auto envelopes = manager.invoke_batch(
      std::span<const dasall::contracts::ToolRequest>(requests), context);

  assert_equal(static_cast<std::size_t>(2U), envelopes.size(),
               "invoke_batch with leading failure should still return all envelopes");
  assert_true(!envelopes[0].tool_result->success.value_or(true),
              "invoke_batch request[0] failure should be localized");
  assert_true(envelopes[1].tool_result->success.value_or(false),
              "invoke_batch request[1] should succeed even after request[0] failed");
}

}  // namespace

int main() {
  try {
    test_batch_invoke_preserves_per_request_independence();
    test_batch_invoke_single_failure_does_not_pollute_others();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
