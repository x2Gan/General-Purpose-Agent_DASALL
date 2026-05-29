#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "AgentOrchestrator.h"
#include "RuntimeDependencySet.h"
#include "logging/LoggingFacade.h"
#include "support/TestAssertions.h"

namespace {

using dasall::infra::InfraContext;
using dasall::infra::LogEvent;
using dasall::infra::LogLevel;
using dasall::infra::logging::ILogDispatchBackend;
using dasall::infra::logging::LogFlushDeadline;
using dasall::infra::logging::LogWriteResult;
using dasall::infra::logging::LoggingFacade;
using dasall::runtime::AgentOrchestrator;
using dasall::runtime::OrchestratorComposition;
using dasall::runtime::RuntimeDependencySet;
using dasall::runtime::StubMainLoopExit;
using dasall::runtime::StubRecoveryExit;
using dasall::tests::support::assert_true;

class RecordingDispatchBackend final : public ILogDispatchBackend {
 public:
  LogWriteResult dispatch(const LogEvent& event) override {
    events_.push_back(event);
    return LogWriteResult::success();
  }

  LogWriteResult flush(const LogFlushDeadline&) override {
    return LogWriteResult::success();
  }

  [[nodiscard]] const std::vector<LogEvent>& events() const {
    return events_;
  }

 private:
  std::vector<LogEvent> events_;
};

[[nodiscard]] dasall::contracts::AgentRequest make_request(
    std::string request_id) {
  dasall::contracts::AgentRequest request;
  request.request_id = std::move(request_id);
  request.session_id = std::string{"session-runtime-logging"};
  request.trace_id = std::string{"trace-runtime-logging"};
  request.user_input = std::string{"emit orchestrator logging for debugging"};
  request.request_channel = dasall::contracts::RequestChannel::Cli;
  request.created_at = 1710000000000;
  return request;
}

[[nodiscard]] bool has_attr(const LogEvent& event,
                            const std::string& key,
                            const std::string& value) {
  const auto it = event.attrs.find(key);
  return it != event.attrs.end() && it->second == value;
}

[[nodiscard]] bool has_key(const LogEvent& event, const std::string& key) {
  return event.attrs.find(key) != event.attrs.end();
}

[[nodiscard]] const LogEvent* find_event(
    const std::vector<LogEvent>& events,
    const std::string& event_name,
    const std::vector<std::pair<std::string, std::string>>& required_attrs = {}) {
  for (const auto& event : events) {
    if (!has_attr(event, "event_name", event_name)) {
      continue;
    }

    bool matches = true;
    for (const auto& [key, value] : required_attrs) {
      if (!has_attr(event, key, value)) {
        matches = false;
        break;
      }
    }

    if (matches) {
      return &event;
    }
  }

  return nullptr;
}

[[nodiscard]] std::shared_ptr<LoggingFacade> make_logger(
    RecordingDispatchBackend** backend_out) {
  auto backend = std::make_unique<RecordingDispatchBackend>();
  *backend_out = backend.get();
  auto logger = std::make_shared<LoggingFacade>(std::move(backend));
  assert_true(logger->init(InfraContext{
                          .request_id = std::string("req-runtime-orchestrator-logging"),
                          .session_id = std::string("session-runtime-orchestrator-logging"),
                          .trace_id = std::string("trace-runtime-orchestrator-logging"),
                          .task_id = std::string("task-runtime-orchestrator-logging"),
                          .parent_task_id =
                              std::string("parent-runtime-orchestrator-logging"),
                          .lease_id = std::string("lease-runtime-orchestrator-logging"),
                      })
                      .ok,
              "agent orchestrator logging unit test should initialize logger");
  return logger;
}

void test_agent_orchestrator_logs_direct_run_lifecycle() {
  RecordingDispatchBackend* backend = nullptr;
  const auto logger = make_logger(&backend);
  auto dependency_set = std::make_shared<RuntimeDependencySet>();
  dependency_set->logger = logger;

  OrchestratorComposition composition;
  composition.runtime_instance_id = "runtime-direct-logging-test";
  composition.dependency_set = dependency_set;
  AgentOrchestrator orchestrator(std::move(composition));

  const auto result = orchestrator.run_once(make_request("req-runtime-log-direct"));
  assert_true(result.agent_result.status == dasall::contracts::AgentResultStatus::Completed,
              "direct path should still complete while logging is enabled");

  const auto& events = backend->events();
  const auto* start_event = find_event(
      events,
      "runtime.orchestrator.run.start",
      {{"operation", "run_once"},
       {"request_id", "req-runtime-log-direct"},
       {"runtime_instance_id", "runtime-direct-logging-test"}});
  assert_true(start_event != nullptr,
              "direct path should log orchestrator run start with correlation attrs");

  const auto* preflight_stage = find_event(
      events,
      "runtime.orchestrator.stage",
      {{"stage", "preflight"}, {"entered", "true"}});
  assert_true(preflight_stage != nullptr,
              "direct path should log preflight stage state transition summary");
    assert_true(has_key(*preflight_stage, "state_before") &&
            has_key(*preflight_stage, "state_after"),
          "direct path preflight log should expose explicit state boundary attrs");

  const auto* finish_event = find_event(
      events,
      "runtime.orchestrator.run.finish",
      {{"agent_status", "Completed"},
       {"final_state", "Completed"},
       {"used_tool_round", "false"},
       {"used_recovery_round", "false"}});
  assert_true(finish_event != nullptr,
              "direct path should log orchestrator finish summary for integration debugging");
    assert_true(has_attr(*finish_event, "stage_count", "5"),
          "direct path finish summary should retain stage_count for path reconstruction");
  assert_true(finish_event->level == LogLevel::Info,
              "completed direct path should emit info-level finish summary");
}

void test_agent_orchestrator_logs_recovery_path_summary() {
  RecordingDispatchBackend* backend = nullptr;
  const auto logger = make_logger(&backend);
  auto dependency_set = std::make_shared<RuntimeDependencySet>();
  dependency_set->logger = logger;

  OrchestratorComposition composition;
  composition.runtime_instance_id = "runtime-recovery-logging-test";
  composition.dependency_set = dependency_set;
  composition.stub_ports.main_loop_exit = StubMainLoopExit::ToolRound;
  composition.stub_ports.recovery_exit = StubRecoveryExit::AbortSafe;
  AgentOrchestrator orchestrator(std::move(composition));

  const auto result = orchestrator.run_once(make_request("req-runtime-log-recovery"));
  assert_true(result.agent_result.status == dasall::contracts::AgentResultStatus::Failed,
              "abort-safe path should still fail deterministically while logging is enabled");

  const auto& events = backend->events();
  const auto* recovery_stage = find_event(
      events,
      "runtime.orchestrator.stage",
      {{"stage", "recovery_round"}, {"entered", "true"}});
  assert_true(recovery_stage != nullptr,
              "recovery path should log recovery_round stage with failed-safe terminal state");
    assert_true(has_key(*recovery_stage, "state_after"),
          "recovery path stage log should retain the resulting runtime state attr");

  const auto* finish_event = find_event(
      events,
      "runtime.orchestrator.run.finish",
      {{"agent_status", "Failed"},
       {"used_tool_round", "true"},
      {"used_recovery_round", "true"}});
  assert_true(finish_event != nullptr,
              "recovery path should log finish summary with recovery-stage debugging fields");
  assert_true(finish_event->level == LogLevel::Error,
              "failed recovery path should emit error-level finish summary");
  assert_true(has_attr(*finish_event, "stage_count", "5"),
              "recovery path finish summary should retain stage_count for path reconstruction");
}

}  // namespace

int main() {
  try {
    test_agent_orchestrator_logs_direct_run_lifecycle();
    test_agent_orchestrator_logs_recovery_path_summary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}