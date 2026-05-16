#include <exception>
#include <iostream>
#include <memory>

#include "AgentFacade.h"
#include "IAgent.h"
#include "agent/AgentResult.h"
#include "RuntimeUnaryFixture.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::AgentResultStatus;
using dasall::runtime::AgentFacade;
using dasall::runtime::IAgent;
using dasall::tests::runtime_fixture::make_agent_request;
using dasall::tests::runtime_fixture::make_init_request;
using dasall::tests::support::assert_true;

void test_runtime_build_liveness_smoke() {
  std::unique_ptr<IAgent> agent = std::make_unique<AgentFacade>();

  const auto init_result = agent->init(
      make_init_request("rt-build-smoke", "desktop_full", "build-liveness-smoke"));
  assert_true(init_result.is_ready(),
              "runtime build liveness smoke should initialize the facade successfully");

  const auto handle_result = agent->handle(make_agent_request(
      "req-build-smoke", "session-build-smoke", "trace-build-smoke", "ping"));
  assert_true(handle_result.status.has_value() &&
                  *handle_result.status == AgentResultStatus::Completed,
              "runtime build liveness smoke should complete a unary request");
  assert_true(handle_result.response_text.has_value() && !handle_result.response_text->empty(),
              "runtime build liveness smoke should emit a non-empty response");

  assert_true(agent->stop(100U),
              "runtime build liveness smoke should stop a live facade successfully");
}

}  // namespace

int main() {
  try {
    test_runtime_build_liveness_smoke();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
