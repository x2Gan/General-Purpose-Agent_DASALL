#include <exception>
#include <iostream>
#include <string>

#include "ToolManager.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::contracts::ToolRequest make_request(
    std::string request_id,
    std::string tool_call_id) {
  return dasall::contracts::ToolRequest{
      .request_id = std::move(request_id),
      .tool_call_id = std::move(tool_call_id),
      .tool_name = std::string("agent.terminal"),
      .invocation_kind = dasall::contracts::ToolInvocationKind::Action,
      .arguments_payload = std::string("{}"),
      .created_at = 1000,
      .goal_id = std::string("goal-1"),
      .worker_task_id = std::string("worker-1"),
      .runtime_budget = std::nullopt,
      .timeout_ms = 2500U,
      .idempotency_key = std::string("idem-1"),
      .tags = std::vector<std::string>{"builtin"},
  };
}

void test_tool_manager_is_instantiable_and_fails_closed_when_runtime_context_is_missing() {
  dasall::tools::ToolManager manager;

  const auto invoke_envelope = manager.invoke(make_request("req-1", "call-1"), {});
  assert_true(invoke_envelope.tool_result.has_value(),
              "invoke should still produce a unified envelope");
  assert_true(!invoke_envelope.tool_result->success.value_or(true),
              "invoke should fail closed when caller context is incomplete");
  assert_true(invoke_envelope.failure_reason_code.has_value(),
              "invoke should explain why the request was denied");
  assert_equal(std::string("tool.manager.profile_missing"),
               *invoke_envelope.failure_reason_code,
               "missing profile snapshot should surface a fail-closed reason");
  assert_true(!invoke_envelope.has_projection(),
              "early fail-closed invocation should not pretend projection exists");

  const auto compensate_envelope = manager.compensate(
      dasall::tools::CompensationRequest{
          .tool_call_id = std::string("call-1"),
          .compensation_action = std::string("noop"),
          .target_ref = std::string("tool://agent.terminal"),
          .reason_code = std::string("test"),
          .evidence_refs = std::vector<std::string>{"evidence://1"},
      },
      {});
  assert_true(compensate_envelope.failure_reason_code.has_value(),
              "compensate should also return a unified failure envelope");
  assert_equal(std::string("tool.manager.compensation_unconfigured"),
               *compensate_envelope.failure_reason_code,
               "skeleton compensate should stay fail-closed until compensation is wired");
}

}  // namespace

int main() {
  try {
    test_tool_manager_is_instantiable_and_fails_closed_when_runtime_context_is_missing();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}