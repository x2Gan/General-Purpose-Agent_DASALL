#include <exception>
#include <filesystem>
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

class ReadyDurableResumeMemoryManager final : public dasall::memory::IMemoryManager {
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
    const auto durable_root = std::filesystem::temp_directory_path() /
                              "dasall-runtime-durable-resume-test";
    (void)std::filesystem::remove_all(durable_root);

    std::string waiting_checkpoint_ref;
    {
      std::unique_ptr<IAgent> writer_agent = std::make_unique<AgentFacade>();
      auto writer_dependency_set = make_waiting_dependency_set();
      writer_dependency_set->durable_state_root = durable_root.string();

      const auto init_result = writer_agent->init(make_init_request(
          "rt-fix-003-durable-writer",
          "desktop_full",
          "runtime-durable-resume-writer",
          writer_dependency_set));
      assert_true(init_result.is_ready(),
                  "durable resume setup requires a ready facade for waiting checkpoint creation");

      const auto waiting_result = writer_agent->handle(
          make_agent_request("req-rt-fix-003-wait",
                             "session-rt-fix-003",
                             "trace-rt-fix-003-wait",
                             "need clarification"));
      assert_true(waiting_result.status.has_value() &&
                      *waiting_result.status == AgentResultStatus::PartiallyCompleted,
                  "durable resume setup should create a waiting checkpoint");
      assert_true(waiting_result.checkpoint_ref.has_value() &&
                      !waiting_result.checkpoint_ref->empty(),
                  "durable resume setup should expose a checkpoint anchor");
      waiting_checkpoint_ref = *waiting_result.checkpoint_ref;
    }

    std::unique_ptr<IAgent> reader_agent = std::make_unique<AgentFacade>();
    auto reader_dependency_set = make_waiting_dependency_set();
    reader_dependency_set->durable_state_root = durable_root.string();

    const auto reader_init_result = reader_agent->init(make_init_request(
        "rt-fix-003-durable-reader",
        "desktop_full",
        "runtime-durable-resume-reader",
        reader_dependency_set));
    assert_true(reader_init_result.is_ready(),
                "durable resume restart requires a ready facade");

    reader_dependency_set->memory_manager = std::make_shared<ReadyDurableResumeMemoryManager>();
    const auto resumed_result = reader_agent->resume(
        make_resume_request("session-rt-fix-003",
                            waiting_checkpoint_ref,
                            "resume-rt-fix-003",
                            "user clarification received",
                            std::string(),
                            "trace-rt-fix-003-resume"));

    assert_true(resumed_result.status.has_value() &&
                    *resumed_result.status == AgentResultStatus::Completed,
                "durable resume should converge to completed after restart");
    assert_true(resumed_result.task_completed == true,
                "durable resume should mark task_completed=true after restart");
    assert_true(resumed_result.checkpoint_ref.has_value() &&
                    resumed_result.checkpoint_ref != waiting_checkpoint_ref,
                "durable resume should emit a fresh completion checkpoint");
    assert_equal("runtime orchestrator skeleton completed",
                 resumed_result.response_text.value_or(std::string()),
                 "durable resume should return the direct-success response");

    (void)std::filesystem::remove_all(durable_root);
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}