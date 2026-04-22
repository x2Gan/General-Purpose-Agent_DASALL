#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "AgentFacade.h"
#include "IAgent.h"
#include "agent/AgentResult.h"
#include "RuntimeUnaryFixture.h"
#include "support/TestAssertions.h"

int main() {
  using dasall::contracts::AgentResultStatus;
  using dasall::runtime::AgentFacade;
  using dasall::runtime::IAgent;
  using dasall::tests::runtime_fixture::make_agent_request;
  using dasall::tests::runtime_fixture::make_incomplete_resume_request;
  using dasall::tests::runtime_fixture::make_init_request;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  try {
    std::unique_ptr<IAgent> agent = std::make_unique<AgentFacade>();

    const auto init_result = agent->init(make_init_request("rt-025", "desktop_full", "surface-test"));
    assert_true(init_result.is_ready(), "runtime facade should accept the minimum valid init request");
    assert_equal("rt-025", init_result.runtime_instance_id, "runtime instance id should round-trip through init result");
    assert_equal("desktop_full", init_result.resolved_profile_id, "profile id should round-trip through init result");

    const auto handle_result = agent->handle(
        make_agent_request("req-025", "session-025", "trace-025", "ping"));
    assert_true(handle_result.status.has_value() &&
                    *handle_result.status == AgentResultStatus::Completed,
                "runtime facade should hand unary requests off to the orchestrator");
    assert_true(handle_result.task_completed == true,
                "runtime facade direct handoff should complete the unary request");
    assert_true(handle_result.request_id.has_value() && *handle_result.request_id == "req-025",
                "handle result should preserve request correlation");
    assert_true(handle_result.trace_id.has_value() && *handle_result.trace_id == "trace-025",
                "handle result should preserve trace correlation");
    assert_true(handle_result.checkpoint_ref.has_value() && !handle_result.checkpoint_ref->empty(),
                "handle result should expose checkpoint anchor from orchestrator");
    assert_true(handle_result.response_text.has_value() &&
                    handle_result.response_text->find("skeleton completed") !=
                        std::string::npos,
                "handle result should expose the orchestrator direct-success response");

    const auto resume_result = agent->resume(make_incomplete_resume_request());
    assert_true(resume_result.status.has_value() &&
                    *resume_result.status == AgentResultStatus::Failed,
                "resume should also fail closed when checkpoint anchors are missing");
    assert_true(resume_result.response_text.has_value() &&
                    resume_result.response_text->find("missing required checkpoint anchors") !=
                        std::string::npos,
                "resume should reject incomplete checkpoint anchors explicitly");

    assert_true(agent->stop(100U), "runtime facade stop should succeed for a live facade instance");
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}