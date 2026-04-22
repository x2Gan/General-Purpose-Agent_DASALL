#include <exception>
#include <iostream>
#include <memory>

#include "AgentFacade.h"
#include "IAgent.h"
#include "RuntimeUnaryFixture.h"
#include "agent/AgentResult.h"
#include "support/TestAssertions.h"

int main() {
  using dasall::contracts::AgentResultStatus;
  using dasall::runtime::AgentFacade;
  using dasall::runtime::IAgent;
  using dasall::tests::runtime_fixture::make_agent_request;
  using dasall::tests::runtime_fixture::make_init_request;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  try {
    std::unique_ptr<IAgent> agent = std::make_unique<AgentFacade>();

    const auto init_result = agent->init(make_init_request("rt-026"));
    assert_true(init_result.is_ready(), "runtime unary fixture integration requires a ready facade");

    const auto result = agent->handle(make_agent_request("req-026", "session-026", "trace-026"));

    assert_true(result.status.has_value() && *result.status == AgentResultStatus::Completed,
                "runtime unary fixture chain should complete successfully");
    assert_true(result.task_completed == true,
                "runtime unary fixture chain should mark task_completed=true");
    assert_true(result.request_id.has_value() && *result.request_id == "req-026",
                "runtime unary fixture chain should preserve request correlation");
    assert_true(result.trace_id.has_value() && *result.trace_id == "trace-026",
                "runtime unary fixture chain should preserve trace correlation");
    assert_true(result.checkpoint_ref.has_value() && !result.checkpoint_ref->empty(),
                "runtime unary fixture chain should materialize a checkpoint anchor");
    assert_true(result.goal_id.has_value() && !result.goal_id->empty(),
                "runtime unary fixture chain should expose a goal id from orchestrator composition");
    assert_equal("runtime orchestrator skeleton completed",
                 result.response_text.value_or(std::string()),
                 "runtime unary fixture chain should return the default direct-success response");
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}