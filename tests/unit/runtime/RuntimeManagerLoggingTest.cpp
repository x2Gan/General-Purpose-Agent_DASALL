#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "AgentTypes.h"
#include "checkpoint/CheckpointManager.h"
#include "logging/LoggingFacade.h"
#include "scheduling/Scheduler.h"
#include "session/SessionManager.h"
#include "support/TestAssertions.h"

namespace {

using dasall::infra::InfraContext;
using dasall::infra::LogEvent;
using dasall::infra::LogLevel;
using dasall::infra::logging::ILogDispatchBackend;
using dasall::infra::logging::LogFlushDeadline;
using dasall::infra::logging::LogWriteResult;
using dasall::infra::logging::LoggingFacade;
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

[[nodiscard]] bool has_attr(
    const LogEvent& event,
    const std::string& key,
    const std::string& value) {
  const auto it = event.attrs.find(key);
  return it != event.attrs.end() && it->second == value;
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
                          .request_id = std::string("req-runtime-manager-logging"),
                          .session_id = std::string("session-runtime-manager-logging"),
                          .trace_id = std::string("trace-runtime-manager-logging"),
                          .task_id = std::string("task-runtime-manager-logging"),
                          .parent_task_id =
                              std::string("parent-runtime-manager-logging"),
                          .lease_id = std::string("lease-runtime-manager-logging"),
                      })
                      .ok,
              "runtime manager logging test should initialize logger");
  return logger;
}

[[nodiscard]] dasall::runtime::StateTransitionOutcome make_waiting_clarify_outcome() {
  return dasall::runtime::make_transition_outcome(
      dasall::runtime::StateTransitionRequest{
          .from_state = dasall::runtime::RuntimeState::Reasoning,
          .requested_to = dasall::runtime::RuntimeState::WaitingClarify,
          .transition_reason = "clarification required",
          .guard_facts = {
              dasall::runtime::TransitionGuardFact::ClarificationNeeded,
              dasall::runtime::TransitionGuardFact::ProfileAllowsClarify,
          },
      },
      dasall::runtime::RuntimeState::WaitingClarify,
      dasall::runtime::StateTransitionCheckpointHint{
          .mutation = dasall::runtime::CheckpointMutationKind::Write,
          .checkpoint_state = dasall::contracts::CheckpointState::Paused,
          .pending_action_required = true,
      });
}

[[nodiscard]] dasall::runtime::SchedulerTicketRequest make_ticket_request(
    const std::string& ticket_id,
    const std::string& request_id,
    const dasall::runtime::SchedulerPriorityClass priority_class,
    const std::optional<std::string>& session_id = std::nullopt) {
  return dasall::runtime::SchedulerTicketRequest{
      .ticket_id = ticket_id,
      .request_id = request_id,
      .session_id = session_id,
      .priority_class = priority_class,
      .cancellation_token = dasall::runtime::CancellationToken(),
      .checkpoint_ref = std::nullopt,
      .queue_key = std::nullopt,
  };
}

void test_session_manager_emits_structured_logs() {
  RecordingDispatchBackend* backend = nullptr;
  const auto logger = make_logger(&backend);

  dasall::runtime::SessionManager manager;
  manager.set_logger(logger, "runtime-manager-logging-test");

  const auto initial_load = manager.load_session(
      dasall::runtime::SessionLoadRequest{
          .session_id = "session-log-001",
          .request_id = "req-log-001",
          .checkpoint_ref = std::nullopt,
          .allow_session_create = true,
      });
  assert_true(initial_load.has_snapshot(),
              "session logging test should create a new session snapshot");

  const auto bind_result = manager.bind_checkpoint_ref(
      dasall::runtime::BindCheckpointRefRequest{
          .session_id = "session-log-001",
          .request_id = "req-log-001",
          .checkpoint_ref = "chk-log-001",
          .fsm_state = dasall::runtime::RuntimeState::WaitingClarify,
          .pending_interaction = dasall::runtime::PendingInteractionState{
              .interaction_kind = dasall::runtime::PendingInteractionKind::Clarify,
              .prompt_token = "prompt-log-001",
              .deadline_ms = 1710000000123,
              .blocking_reason = "await clarification",
              .resume_channel = "user_reply",
              .input_schema_hint = "text/plain",
          },
      });
  assert_true(bind_result.persisted,
              "session logging test should bind checkpoint ref successfully");

  const auto bound_snapshot = manager.load_session(
      dasall::runtime::SessionLoadRequest{
          .session_id = "session-log-001",
          .request_id = "req-log-001",
          .checkpoint_ref = std::string("chk-log-001"),
          .allow_session_create = false,
      });
  assert_true(bound_snapshot.has_snapshot(),
              "session logging test should reload bound snapshot");

  const auto rejected_resume = manager.build_resume_seed(
      dasall::runtime::BuildResumeSeedRequest{
          .session_snapshot = *bound_snapshot.snapshot,
          .checkpoint_ref = "chk-log-other",
          .resume_token = dasall::runtime::make_resume_binding_token(
              "session-log-001",
              "chk-log-other"),
          .resume_reason = "resume with wrong anchor",
          .policy_snapshot_ref = std::nullopt,
      });
  assert_true(!rejected_resume.built(),
              "session logging test should exercise a rejected resume seed path");

  const auto& events = backend->events();
  const auto* load_event = find_event(
      events,
      "runtime.session.load",
      {{"session_id", "session-log-001"},
       {"created_new_session", "true"},
       {"runtime_instance_id", "runtime-manager-logging-test"}});
  assert_true(load_event != nullptr,
              "session manager should log load_session correlation and creation attrs");
  assert_true(load_event->level == LogLevel::Info,
              "successful session load should emit info-level log");
  assert_true(has_attr(*load_event, "fsm_state", "Idle"),
              "session load log should expose snapshot state summary");

  const auto* bind_event = find_event(
      events,
      "runtime.session.bind_checkpoint_ref",
      {{"session_id", "session-log-001"},
       {"checkpoint_ref", "chk-log-001"},
       {"persisted", "true"}});
  assert_true(bind_event != nullptr,
              "session manager should log checkpoint binding facts");
  assert_true(has_attr(*bind_event, "pending_interaction_kind", "Clarify"),
              "session binding log should expose pending interaction kind");

  const auto* resume_event = find_event(
      events,
      "runtime.session.build_resume_seed",
      {{"session_id", "session-log-001"},
       {"checkpoint_ref", "chk-log-other"},
       {"built", "false"}});
  assert_true(resume_event != nullptr,
              "session manager should log rejected resume seed path");
  assert_true(resume_event->level == LogLevel::Warn,
              "rejected resume seed should emit warn-level log");
  assert_true(
      has_attr(
          *resume_event,
          "error_code",
          std::to_string(static_cast<int>(
              dasall::runtime::RuntimeErrorCode::RT_E_401_SESSION_INCONSISTENT))),
      "rejected resume seed log should expose error code for debugging");
}

void test_checkpoint_manager_emits_structured_logs() {
  RecordingDispatchBackend* backend = nullptr;
  const auto logger = make_logger(&backend);

  dasall::runtime::CheckpointManager manager;
  manager.set_logger(logger, "runtime-manager-logging-test");

  const auto build_result = manager.build_checkpoint(
      dasall::runtime::CheckpointBuildRequest{
          .transition_outcome = make_waiting_clarify_outcome(),
          .checkpoint_id = "chk-log-001",
          .step_id = "clarify-step-log",
          .working_memory_snapshot = "wm:clarify:log",
          .pending_action = std::string("wait for user clarification"),
          .request_id = std::string("req-log-001"),
          .goal_id = std::nullopt,
          .belief_state_ref = std::nullopt,
          .retry_count = 1,
          .created_at_ms = 1710000000456,
          .runtime_budget_snapshot = std::nullopt,
          .tags = {"audit=runtime-manager-logging"},
      });
  assert_true(build_result.built(),
              "checkpoint logging test should build a consistent checkpoint");

  const auto save_result = manager.save(*build_result.checkpoint, build_result.runtime_budget_snapshot);
  assert_true(save_result.persisted,
              "checkpoint logging test should persist the checkpoint");

  const auto missing_load = manager.load("chk-log-missing");
  assert_true(!missing_load.loaded(),
              "checkpoint logging test should exercise a missing checkpoint load path");

  auto terminal_checkpoint = *build_result.checkpoint;
  terminal_checkpoint.state = dasall::contracts::CheckpointState::Succeeded;
  const auto rejected_resume = manager.make_resume_plan(terminal_checkpoint);
  assert_true(rejected_resume.rejected(),
              "checkpoint logging test should exercise a rejected resume plan path");

  const auto& events = backend->events();
  const auto* build_event = find_event(
      events,
      "runtime.checkpoint.build",
      {{"checkpoint_id", "chk-log-001"},
       {"built", "true"},
       {"checkpoint_state", "Paused"}});
  assert_true(build_event != nullptr,
              "checkpoint manager should log build success summary");
  assert_true(build_event->level == LogLevel::Info,
              "successful checkpoint build should emit info-level log");

  const auto* load_event = find_event(
      events,
      "runtime.checkpoint.load",
      {{"checkpoint_ref", "chk-log-missing"},
       {"loaded", "false"}});
  assert_true(load_event != nullptr,
              "checkpoint manager should log missing checkpoint load path");
  assert_true(load_event->level == LogLevel::Error,
              "missing checkpoint load should emit error-level log");
  assert_true(
      has_attr(
          *load_event,
          "error_code",
          std::to_string(static_cast<int>(
              dasall::runtime::RuntimeErrorCode::RT_E_410_CHECKPOINT_CORRUPT))),
      "checkpoint load log should expose corrupt/not-found error code");

  const auto* resume_event = find_event(
      events,
      "runtime.checkpoint.make_resume_plan",
      {{"checkpoint_ref", "chk-log-001"},
       {"resume_seed_present", "false"},
       {"resumable", "false"}});
  assert_true(resume_event != nullptr,
              "checkpoint manager should log rejected resume plan path");
  assert_true(has_attr(*resume_event, "resume_plan_violation", "UnsupportedCheckpointState"),
              "resume plan log should expose rejection classification");
}

void test_scheduler_emits_structured_logs() {
  RecordingDispatchBackend* backend = nullptr;
  const auto logger = make_logger(&backend);

  dasall::runtime::Scheduler scheduler;
  scheduler.set_logger(logger, "runtime-manager-logging-test");

  const auto accepted = scheduler.enqueue(make_ticket_request(
      "ticket-log-001",
      "req-ticket-log-001",
      dasall::runtime::SchedulerPriorityClass::ForegroundInteractive,
      std::string("session-log-001")));
  assert_true(accepted.accepted,
              "scheduler logging test should accept initial foreground ticket");

  const auto rejected = scheduler.enqueue(make_ticket_request(
      "ticket-log-002",
      "req-ticket-log-002",
      dasall::runtime::SchedulerPriorityClass::ForegroundInteractive,
      std::string("session-log-001")));
  assert_true(!rejected.accepted,
              "scheduler logging test should exercise foreground overflow path");

  const auto acquired = scheduler.acquire_worker(
      dasall::runtime::AcquireWorkerRequest{
          .worker_budget = dasall::runtime::WorkerLeaseBudget{.max_workers = 1, .busy_workers = 0},
          .preferred_priority_class =
              dasall::runtime::SchedulerPriorityClass::ForegroundInteractive,
          .preferred_ticket_id = std::nullopt,
      });
  assert_true(acquired.acquired,
              "scheduler logging test should acquire worker for accepted ticket");

  const auto released = scheduler.release_worker(
      dasall::runtime::ReleaseWorkerRequest{
          .ticket = *acquired.ticket,
          .worker_completed = true,
      });
  assert_true(released.released,
              "scheduler logging test should release acquired worker");

  const auto saturated = scheduler.acquire_worker(
      dasall::runtime::AcquireWorkerRequest{
          .worker_budget = dasall::runtime::WorkerLeaseBudget{.max_workers = 1, .busy_workers = 1},
          .preferred_priority_class = std::nullopt,
          .preferred_ticket_id = std::nullopt,
      });
  assert_true(!saturated.acquired,
              "scheduler logging test should exercise worker saturation path");

  const auto state = scheduler.backpressure_state();
    assert_true(state.overloaded(),
                            "scheduler logging test should preserve the saturated worker-budget snapshot");

  const auto& events = backend->events();
  const auto* accepted_event = find_event(
      events,
      "runtime.scheduler.enqueue",
      {{"ticket_id", "ticket-log-001"},
       {"accepted", "true"},
       {"priority_class", "ForegroundInteractive"}});
  assert_true(accepted_event != nullptr,
              "scheduler should log accepted enqueue facts");
  assert_true(accepted_event->level == LogLevel::Info,
              "accepted enqueue should emit info-level log");

  const auto* rejected_event = find_event(
      events,
      "runtime.scheduler.enqueue",
      {{"ticket_id", "ticket-log-002"},
       {"accepted", "false"},
       {"applied_overflow_disposition", "RejectNew"}});
  assert_true(rejected_event != nullptr,
              "scheduler should log rejected enqueue overflow facts");
  assert_true(rejected_event->level == LogLevel::Warn,
              "foreground overflow should emit warn-level log");
  assert_true(has_attr(*rejected_event, "dominant_signal", "ForegroundBusy"),
              "rejected enqueue log should expose dominant backpressure signal");

  const auto* release_event = find_event(
      events,
      "runtime.scheduler.release_worker",
      {{"ticket_id", "ticket-log-001"},
       {"released", "true"}});
  assert_true(release_event != nullptr,
              "scheduler should log worker release summary");

  const auto* saturated_event = find_event(
      events,
      "runtime.scheduler.acquire_worker",
      {{"acquired", "false"},
       {"dominant_signal", "WorkerPoolSaturated"}});
  assert_true(saturated_event != nullptr,
              "scheduler should log worker saturation path");
  assert_true(saturated_event->level == LogLevel::Warn,
              "worker saturation should emit warn-level log");

  const auto* state_event = find_event(
      events,
      "runtime.scheduler.backpressure_state",
      {{"overloaded", "true"}, {"busy_workers", "1"}, {"max_workers", "1"}});
  assert_true(state_event != nullptr,
              "scheduler should log backpressure snapshot summary");
}

}  // namespace

int main() {
  try {
    test_session_manager_emits_structured_logs();
    test_checkpoint_manager_emits_structured_logs();
    test_scheduler_emits_structured_logs();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}