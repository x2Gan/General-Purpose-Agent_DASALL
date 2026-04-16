#include <exception>
#include <iostream>
#include <vector>

#include "ToolManager.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::contracts::ToolRequest make_request(
    int request_index,
    std::string request_id,
    std::string tool_call_id) {
  return dasall::contracts::ToolRequest{
      .request_id = std::move(request_id),
      .tool_call_id = std::move(tool_call_id),
      .tool_name = std::string("agent.terminal"),
      .invocation_kind = dasall::contracts::ToolInvocationKind::Action,
      .arguments_payload = std::string("{\"index\":") + std::to_string(request_index) + "}",
      .created_at = 1000 + request_index,
      .goal_id = std::string("goal-batch"),
      .worker_task_id = std::string("worker-batch"),
      .runtime_budget = std::nullopt,
      .timeout_ms = 2500U,
      .idempotency_key = std::string("idem-") + std::to_string(request_index),
      .tags = std::vector<std::string>{"builtin"},
  };
}

void test_invoke_batch_preserves_request_level_isolation_and_order() {
  dasall::tools::ToolManager manager;
  const std::vector<dasall::contracts::ToolRequest> requests{
      make_request(1, "req-1", "call-1"),
      make_request(2, "req-2", "call-2"),
  };

  const auto envelopes = manager.invoke_batch(requests, {});
  assert_equal(static_cast<int>(requests.size()), static_cast<int>(envelopes.size()),
               "invoke_batch should return one envelope per request");

  for (std::size_t index = 0; index < requests.size(); ++index) {
    const auto& request = requests[index];
    const auto& envelope = envelopes[index];

    assert_true(envelope.tool_result.has_value(),
                "batch skeleton should preserve a tool_result for each request");
    assert_equal(*request.request_id, *envelope.tool_result->request_id,
                 "batch invoke should preserve request identity per item");
    assert_equal(*request.tool_call_id, *envelope.tool_result->tool_call_id,
                 "batch invoke should preserve tool_call identity per item");
    assert_true(envelope.failure_reason_code.has_value(),
                "batch invoke should keep a per-request failure code while the pipeline is stubbed");
    assert_equal(std::string("tool.manager.pipeline_unconfigured"),
                 *envelope.failure_reason_code,
                 "batch skeleton should fail each request independently with the same stub reason");
  }
}

}  // namespace

int main() {
  try {
    test_invoke_batch_preserves_request_level_isolation_and_order();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}