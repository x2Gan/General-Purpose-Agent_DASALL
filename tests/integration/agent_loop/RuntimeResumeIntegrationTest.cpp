#include <exception>
#include <iostream>
#include <memory>
#include <string>

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
  using dasall::tests::runtime_fixture::make_resume_request;
  using dasall::tests::runtime_fixture::make_waiting_dependency_set;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  try {
    std::unique_ptr<IAgent> agent = std::make_unique<AgentFacade>();

    const auto init_result = agent->init(make_init_request(
        "rt-028-resume",
        "desktop_full",
        "runtime-resume-fixture",
        make_waiting_dependency_set()));
    assert_true(init_result.is_ready(), "runtime resume integration requires a ready facade");

    const auto waiting_result = agent->handle(
        make_agent_request("req-028-wait", "session-028", "trace-028", "need clarification"));

    assert_true(waiting_result.status.has_value() &&
                    *waiting_result.status == AgentResultStatus::PartiallyCompleted,
                "waiting integration path should keep the turn resumable");
    assert_true(waiting_result.task_completed == false,
                "waiting integration path should not mark the task completed");
    assert_true(waiting_result.checkpoint_ref.has_value() && !waiting_result.checkpoint_ref->empty(),
                "waiting integration path should expose a resumable checkpoint anchor");
    assert_true(waiting_result.response_text.has_value() &&
                    waiting_result.response_text->find("waiting") != std::string::npos,
                "waiting integration path should expose the waiting response text");

    const auto initial_checkpoint_ref = *waiting_result.checkpoint_ref;
    const auto resumed_result = agent->resume(
        make_resume_request("session-028",
                            initial_checkpoint_ref,
                            "resume-028",
                            "user clarification received",
                            "resume-token-028",
                            "trace-resume-028"));

    assert_true(resumed_result.status.has_value() &&
                    *resumed_result.status == AgentResultStatus::Completed,
                "resume integration path should converge back to completed");
    assert_true(resumed_result.task_completed == true,
                "resume integration path should mark task_completed=true");
    assert_true(resumed_result.checkpoint_ref.has_value() &&
                    resumed_result.checkpoint_ref != waiting_result.checkpoint_ref,
                "resume integration path should emit a fresh completion checkpoint");
    assert_true(resumed_result.goal_id.has_value() && !resumed_result.goal_id->empty(),
                "resume integration path should expose the orchestrator goal id");
    assert_equal("runtime orchestrator skeleton completed",
                 resumed_result.response_text.value_or(std::string()),
                 "resume integration path should return the direct-success response");
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}