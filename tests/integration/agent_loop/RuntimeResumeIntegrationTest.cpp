#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "AgentFacade.h"
#include "IMemoryManager.h"
#include "IAgent.h"
#include "RuntimeUnaryFixture.h"
#include "agent/AgentResult.h"
#include "support/TestAssertions.h"

namespace {

class ReadyResumeMemoryManager final : public dasall::memory::IMemoryManager {
 public:
  dasall::contracts::ResultCode init(const dasall::memory::MemoryConfig&) override {
    return static_cast<dasall::contracts::ResultCode>(0);
  }

  void shutdown() noexcept override {}

  [[nodiscard]] dasall::memory::ContextAssemblyResult prepare_context(
      const dasall::memory::MemoryContextRequest& request) override {
    dasall::memory::ContextAssemblyResult result;
    result.context_packet.request_id = request.request_id;
    result.context_packet.user_turn = request.goal_summary;
    result.context_packet.current_goal_summary = request.goal_summary;
    result.context_packet.recent_history = std::vector<std::string>{};
    result.context_packet.latest_observation_digest_summary =
        request.latest_observation_digest_summary;
    result.context_packet.active_tools = request.visible_tools;
    return result;
  }

  [[nodiscard]] dasall::memory::WritebackResult write_back(
      const dasall::memory::MemoryWritebackRequest&) override {
    return {};
  }

  [[nodiscard]] dasall::memory::WorkingMemoryExportResult export_working_memory_snapshot(
      const dasall::memory::WorkingMemoryExportRequest&) override {
    return {};
  }

  [[nodiscard]] dasall::memory::MaintenanceReport run_maintenance(
      const dasall::memory::MaintenanceRequest&) override {
    return {};
  }
};

}  // namespace

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

    auto dependency_set = make_waiting_dependency_set();

    const auto init_result = agent->init(make_init_request(
        "rt-028-resume",
        "desktop_full",
        "runtime-resume-fixture",
      dependency_set));
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
    dependency_set->memory_manager = std::make_shared<ReadyResumeMemoryManager>();
    const auto resumed_result = agent->resume(
        make_resume_request("session-028",
                            initial_checkpoint_ref,
                            "resume-028",
                            "user clarification received",
                            std::string(),
                            "trace-resume-028"));
    const auto resumed_response_text =
        resumed_result.response_text.value_or(std::string("<missing response_text>"));

    assert_true(resumed_result.status.has_value() &&
                    *resumed_result.status == AgentResultStatus::Completed,
                "resume integration path should converge back to completed: " +
                    resumed_response_text);
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

    std::unique_ptr<IAgent> rejecting_agent = std::make_unique<AgentFacade>();
    auto rejecting_dependency_set = make_waiting_dependency_set();
    const auto rejecting_init_result = rejecting_agent->init(make_init_request(
      "rt-028-resume-reject",
      "desktop_full",
      "runtime-resume-fixture",
      rejecting_dependency_set));
    assert_true(rejecting_init_result.is_ready(),
          "negative resume integration requires a ready facade");

    const auto rejecting_waiting_result = rejecting_agent->handle(
      make_agent_request("req-028-wait-reject",
                 "session-028-reject",
                 "trace-028-reject",
                 "need clarification"));
    assert_true(rejecting_waiting_result.checkpoint_ref.has_value() &&
            !rejecting_waiting_result.checkpoint_ref->empty(),
          "negative resume integration path should expose a resumable checkpoint");

    rejecting_dependency_set->memory_manager = std::make_shared<ReadyResumeMemoryManager>();
    const auto rejected_resume_result = rejecting_agent->resume(
      make_resume_request("session-028-reject",
                *rejecting_waiting_result.checkpoint_ref,
                "resume-028-reject",
                "user clarification received",
                "resume-token-mismatch",
                "trace-resume-028-reject"));

    assert_true(rejected_resume_result.status.has_value() &&
            *rejected_resume_result.status == AgentResultStatus::Failed,
          "resume integration should reject mismatched resume tokens");
    assert_true(rejected_resume_result.task_completed == false,
          "mismatched resume token must not mark task_completed=true");
    assert_true(rejected_resume_result.response_text.has_value() &&
            rejected_resume_result.response_text->find(
              "token does not match waiting checkpoint binding") != std::string::npos,
          "mismatched resume token should surface a binding rejection detail");
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}