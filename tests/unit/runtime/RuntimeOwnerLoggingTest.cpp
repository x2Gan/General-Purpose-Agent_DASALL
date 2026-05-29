#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "AgentFacade.h"
#include "RuntimeDependencySet.h"
#include "RuntimePolicySnapshot.h"
#include "RuntimeUnaryFixture.h"
#include "health/RuntimeHealthProbe.h"
#include "logging/LoggingFacade.h"
#include "recovery/RecoveryManager.h"
#include "safety/SafeModeController.h"
#include "support/TestAssertions.h"

namespace {

using dasall::infra::InfraContext;
using dasall::infra::LogEvent;
using dasall::infra::LogLevel;
using dasall::infra::logging::ILogDispatchBackend;
using dasall::infra::logging::LogFlushDeadline;
using dasall::infra::logging::LoggingFacade;
using dasall::infra::logging::LogWriteResult;
using dasall::runtime::IRuntimeHealthSignalProvider;
using dasall::runtime::RuntimeHealthProbe;
using dasall::runtime::RuntimeHealthProbeOptions;
using dasall::runtime::RuntimeHealthSample;
using dasall::tests::support::assert_true;

class MutableHealthSignalProvider final : public IRuntimeHealthSignalProvider {
 public:
  explicit MutableHealthSignalProvider(RuntimeHealthSample sample)
      : sample_(std::move(sample)) {}

  RuntimeHealthSample sample(std::int64_t) override {
    return sample_;
  }

  void set_sample(RuntimeHealthSample sample) {
    sample_ = std::move(sample);
  }

 private:
  RuntimeHealthSample sample_;
};

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

[[nodiscard]] std::string attr_value(
    const LogEvent& event,
    const std::string& key) {
  const auto it = event.attrs.find(key);
  return it == event.attrs.end() ? std::string() : it->second;
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

[[nodiscard]] std::size_t count_events(
    const std::vector<LogEvent>& events,
    const std::string& event_name) {
  return static_cast<std::size_t>(std::count_if(
      events.begin(),
      events.end(),
      [&event_name](const LogEvent& event) {
        return has_attr(event, "event_name", event_name);
      }));
}

[[nodiscard]] std::shared_ptr<LoggingFacade> make_logger(
    RecordingDispatchBackend** backend_out) {
  auto backend = std::make_unique<RecordingDispatchBackend>();
  *backend_out = backend.get();
  auto logger = std::make_shared<LoggingFacade>(std::move(backend));
  assert_true(logger->init(InfraContext{
                          .request_id = std::string("req-runtime-owner-logging"),
                          .session_id = std::string("session-runtime-owner-logging"),
                          .trace_id = std::string("trace-runtime-owner-logging"),
                          .task_id = std::string("task-runtime-owner-logging"),
                          .parent_task_id =
                              std::string("parent-runtime-owner-logging"),
                          .lease_id = std::string("lease-runtime-owner-logging"),
                      })
                      .ok,
              "runtime owner logging test should initialize logger");
  return logger;
}

[[nodiscard]] std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot> make_policy_snapshot(
    const bool allow_model_failover,
    const bool allow_budget_degrade,
    const std::vector<std::string>& fallback_chain,
    const bool safe_mode_enabled = true) {
  using dasall::profiles::CapabilityCachePolicy;
  using dasall::profiles::DegradePolicy;
  using dasall::profiles::ExecutionPolicy;
  using dasall::profiles::ModelProfile;
  using dasall::profiles::ModelRoutePolicy;
  using dasall::profiles::OpsPolicy;
  using dasall::profiles::PromptPolicy;
  using dasall::profiles::RuntimePolicySnapshot;
  using dasall::profiles::TimeoutBudget;
  using dasall::profiles::TimeoutPolicy;
  using dasall::profiles::TokenBudgetPolicy;

  return std::make_shared<RuntimePolicySnapshot>(
      22U,
      "desktop_full",
      dasall::contracts::RuntimeBudget{
          .max_tokens = 4096U,
          .max_turns = 8U,
          .max_tool_calls = 4U,
          .max_latency_ms = 2500U,
          .max_replan_count = 2U,
      },
      ModelProfile{
          .stage_routes = {
              {"default",
               ModelRoutePolicy{
                   .route = "gpt-main",
                   .fallback_route = std::string("gpt-fallback"),
                   .streaming_enabled = false,
               }},
          },
      },
      TokenBudgetPolicy{
          .max_input_tokens = 2048U,
          .max_output_tokens = 1024U,
          .max_history_turns = 8U,
          .compression_threshold = 512U,
      },
      PromptPolicy{
          .allowed_prompt_releases = {"stable"},
          .trusted_sources = {"profiles"},
          .tool_visibility_rules = {"default"},
      },
      CapabilityCachePolicy{
          .refresh_interval_ms = 1000,
          .expire_after_ms = 5000,
          .stale_read_allowed = true,
          .failure_backoff_ms = 200,
      },
      DegradePolicy{
          .fallback_chain = fallback_chain,
          .allow_model_failover = allow_model_failover,
          .allow_budget_degrade = allow_budget_degrade,
      },
      TimeoutPolicy{
          .llm = TimeoutBudget{.timeout_ms = 1500, .retry_budget = 1U, .circuit_breaker_threshold = 3U},
          .tool = TimeoutBudget{.timeout_ms = 1200, .retry_budget = 1U, .circuit_breaker_threshold = 3U},
          .mcp = TimeoutBudget{.timeout_ms = 1800, .retry_budget = 2U, .circuit_breaker_threshold = 3U},
          .workflow = TimeoutBudget{.timeout_ms = 2500, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
      },
      ExecutionPolicy{
          .requires_high_risk_confirmation = true,
          .safe_mode_enabled = safe_mode_enabled,
          .audit_level = "strict",
          .allowed_tool_domains = {"default"},
      },
      OpsPolicy{
          .log_level = "info",
          .metrics_granularity = "full",
          .trace_sample_ratio = 0.5,
          .remote_diagnostics_enabled = false,
          .upgrade_strategy = "manual",
      });
}

[[nodiscard]] dasall::contracts::RecoveryRequest make_recovery_request(
    const bool replay_safe,
    const dasall::contracts::ReflectionDecisionKind decision_kind =
        dasall::contracts::ReflectionDecisionKind::RetryStep,
    const bool safe_to_replan = true,
    const bool budget_exhausted = false) {
  const dasall::contracts::ErrorInfo error_info{
      .failure_type = dasall::contracts::ResultCodeCategory::Runtime,
      .retryable = true,
      .safe_to_replan = safe_to_replan,
      .details = {
          .code = 5001,
          .message = "tool execution failed",
          .stage = "tool_execution",
      },
      .source_ref = {
          .ref_type = "tool_call",
          .ref_id = "tool-42",
      },
  };

  return dasall::contracts::RecoveryRequest{
      .reflection_decision = dasall::contracts::ReflectionDecision{
          .request_id = std::string("req-200"),
          .decision_kind = decision_kind,
          .rationale = std::string("reflection recommends retrying the failed step"),
          .goal_id = std::string("goal-200"),
          .confidence = 0.8F,
          .relevant_observation_refs = std::vector<std::string>{"obs-200"},
          .hint_ref = std::string("hint-200"),
          .created_at = 1700000200,
          .tags = std::vector<std::string>{"unit=recovery"},
      },
      .error_info = error_info,
      .latest_observation = dasall::contracts::Observation{
          .observation_id = std::string("obs-200"),
          .source = dasall::contracts::ObservationSource::ToolExecution,
          .success = false,
          .payload = std::string("{}"),
          .created_at = 1700000201,
          .error = error_info,
          .side_effects = std::nullopt,
          .tool_call_id = std::string("tool-42"),
          .worker_task_id = std::nullopt,
          .request_id = std::string("req-200"),
          .goal_id = std::string("goal-200"),
          .duration_ms = 81,
          .tags = std::vector<std::string>{"unit=recovery"},
      },
      .checkpoint = dasall::contracts::Checkpoint{
          .checkpoint_id = std::string("chk-200"),
          .state = dasall::contracts::CheckpointState::Running,
          .step_id = std::string("tool-call"),
          .working_memory_snapshot = std::string("wm:recovery:1"),
          .pending_action = std::string(),
          .request_id = std::string("req-200"),
          .goal_id = std::string("goal-200"),
          .belief_state_ref = std::string("belief-200"),
          .retry_count = 1,
          .created_at = 1700000202,
          .tags = std::vector<std::string>{
            "rt.schema_version=1",
            "rt.fsm_state_enum_version=1",
            "rt.budget_schema_version=1",
          },
      },
      .idempotency_and_side_effect_report =
          dasall::contracts::IdempotencyAndSideEffectReport{
              .replay_safe = replay_safe,
              .idempotency_key = std::string("idem-200"),
              .side_effects_present = false,
              .non_replayable_reason = replay_safe
                                           ? std::nullopt
                                           : std::optional<std::string>(
                                                 "irreversible side effect already observed"),
          },
      .retry_count = 1,
      .runtime_budget_snapshot = budget_exhausted
          ? std::optional<dasall::contracts::BudgetSnapshot>(
                dasall::contracts::BudgetSnapshot{
                    .snapshot_at_ms = 1700000203,
                    .entries = {
                        {.budget_type = dasall::contracts::BudgetType::Replan,
                         .current = 2,
                         .max = 1,
                         .remaining = -1,
                         .reject_reason = std::string("replan exhausted")},
                    },
                    .overall_reject_reason = std::string("budget exhausted"),
                })
          : std::nullopt,
  };
}

void test_recovery_manager_emits_structured_logs_without_leaking_retry_tokens() {
  RecordingDispatchBackend* backend = nullptr;
  const auto logger = make_logger(&backend);

  dasall::runtime::RecoveryManager manager;
  manager.set_logger(logger, "runtime-owner-logging-test");

  const auto admitted_plan = manager.evaluate(make_recovery_request(true));
  const auto admitted_outcome = manager.execute(admitted_plan);
  const auto admitted_apply = manager.apply(admitted_outcome);
  assert_true(admitted_apply.applied,
              "recovery logging test should apply admitted recovery outcome");

  const auto replan_plan = manager.evaluate(
      make_recovery_request(true, dasall::contracts::ReflectionDecisionKind::Replan));
  assert_true(replan_plan.executable(),
              "recovery logging test should exercise replan admission path");

  const auto degraded_plan = manager.evaluate(
      make_recovery_request(true,
                           dasall::contracts::ReflectionDecisionKind::RetryStep,
                           true,
                           true));
  const auto degraded_outcome = manager.execute(degraded_plan);
  const auto degraded_apply = manager.apply(degraded_outcome);
  assert_true(degraded_apply.applied,
              "recovery logging test should apply degraded recovery outcome");

  const auto& events = backend->events();
  const auto* evaluate_event = find_event(
      events,
      "runtime.recovery.evaluate",
      {{"request_id", "req-200"},
       {"checkpoint_ref", "chk-200"},
       {"admission", "Admit"},
       {"planned_action", "retry_step"},
       {"runtime_instance_id", "runtime-owner-logging-test"}});
  assert_true(evaluate_event != nullptr,
              "recovery manager should log admitted recovery evaluation facts");
  assert_true(evaluate_event->level == LogLevel::Info,
              "admitted recovery evaluation should emit info-level log");
  assert_true(has_attr(*evaluate_event, "resume_plan_present", "true"),
              "recovery evaluation log should expose resume plan availability");
  assert_true(attr_value(*evaluate_event, "detail").find("idem-200") == std::string::npos,
              "recovery evaluation log should redact retry token values from detail");

  const auto* replan_event = find_event(
      events,
      "runtime.recovery.evaluate",
      {{"planned_action", "replan"},
       {"reflection_decision_kind", "Replan"}});
  assert_true(replan_event != nullptr,
              "recovery manager should log replan evaluation facts");
  assert_true(attr_value(*replan_event, "detail").find("replan:chk-200:2") ==
                  std::string::npos,
              "replan evaluation log should redact generated retry token values");

  const auto* execute_event = find_event(
      events,
      "runtime.recovery.execute",
      {{"executed_action", "degrade"},
       {"final_runtime_state", "Degraded"}});
  assert_true(execute_event != nullptr,
              "recovery manager should log degraded execution outcome");
  assert_true(execute_event->level == LogLevel::Warn,
              "degraded recovery execution should emit warn-level log");

  const auto* apply_event = find_event(
      events,
      "runtime.recovery.apply",
      {{"executed_action", "degrade"},
       {"applied", "true"}});
  assert_true(apply_event != nullptr,
              "recovery manager should log recovery apply result");
  assert_true(has_attr(
                  *apply_event,
                  "error_code",
                  std::to_string(static_cast<int>(
                      dasall::runtime::RuntimeErrorCode::RT_E_511_DEGRADE_ENTERED))),
              "recovery apply log should expose degraded-mode error mapping");
}

void test_safe_mode_controller_emits_structured_logs_for_entry_and_exit() {
  RecordingDispatchBackend* backend = nullptr;
  const auto logger = make_logger(&backend);

  dasall::runtime::SafeModeController controller(make_policy_snapshot(
      false,
      true,
      {"allow_budget_degrade", "abort_safe"}));
  controller.set_logger(logger, "runtime-owner-logging-test");

  const auto entry_decision = controller.evaluate_entry(
      dasall::runtime::SafeModeTrigger{
          .trigger_kind = dasall::runtime::SafeModeTriggerKind::BudgetExhausted,
          .budget_decision = dasall::runtime::make_budget_rejected_decision(
              dasall::runtime::BudgetViolationClass::LatencyExhausted,
              "latency budget exhausted"),
          .recovery_outcome = std::nullopt,
          .error_code = std::nullopt,
          .health_signal = std::nullopt,
          .detail = "latency budget exhausted",
      });
  assert_true(entry_decision.transition_required,
              "safe mode logging test should transition into degraded mode");

  const auto exit_decision = controller.evaluate_exit(
      dasall::runtime::SafeModeExitRequest{
          .dependencies_healthy = true,
          .watchdog_healthy = true,
          .operator_cleared = true,
          .budget_restored = true,
          .detail = "operator cleared degraded mode",
      });
  assert_true(exit_decision.transition_required,
              "safe mode logging test should transition back to normal mode");

  const auto& events = backend->events();
  const auto* entry_event = find_event(
      events,
      "runtime.safe_mode.evaluate_entry",
      {{"trigger_kind", "BudgetExhausted"},
       {"previous_mode", "Normal"},
       {"target_mode", "Degraded"},
       {"action", "EnterDegraded"},
       {"runtime_instance_id", "runtime-owner-logging-test"}});
  assert_true(entry_event != nullptr,
              "safe mode controller should log degraded entry facts");
  assert_true(entry_event->level == LogLevel::Warn,
              "safe mode entry should emit warn-level log");
  assert_true(has_attr(*entry_event, "selected_fallback", "allow_budget_degrade"),
              "safe mode entry log should preserve selected fallback step");

  const auto* exit_event = find_event(
      events,
      "runtime.safe_mode.evaluate_exit",
      {{"previous_mode", "Degraded"},
       {"target_mode", "Normal"},
       {"action", "ExitToNormal"},
       {"transition_required", "true"}});
  assert_true(exit_event != nullptr,
              "safe mode controller should log exit-to-normal facts");
  assert_true(exit_event->level == LogLevel::Info,
              "safe mode exit should emit info-level log");
}

void test_agent_facade_emits_entry_logs_without_user_input() {
  RecordingDispatchBackend* backend = nullptr;
  const auto logger = make_logger(&backend);

  auto dependency_set = dasall::tests::runtime_fixture::make_dependency_set();
  dependency_set->logger = logger;

  dasall::runtime::AgentFacade facade;
  const auto init_result = facade.init(
      dasall::tests::runtime_fixture::make_init_request(
          "rt-facade-log",
          "desktop_full",
          "owner-logging",
          dependency_set));
  assert_true(init_result.is_ready(),
              "facade logging test should initialize successfully");

  const auto handle_result = facade.handle(
      dasall::tests::runtime_fixture::make_agent_request(
          "req-facade-log",
          "session-facade-log",
          "trace-facade-log",
          "token=must-not-appear-in-facade-log"));
  assert_true(handle_result.status ==
                  std::optional<dasall::contracts::AgentResultStatus>(
                      dasall::contracts::AgentResultStatus::Completed),
              "facade logging test should complete a unary request");

  const auto resume_result = facade.resume(
      dasall::tests::runtime_fixture::make_incomplete_resume_request(
          "resume-facade-log",
          "session-facade-log",
          "trace-facade-log"));
  assert_true(resume_result.status ==
                  std::optional<dasall::contracts::AgentResultStatus>(
                      dasall::contracts::AgentResultStatus::Failed),
              "facade logging test should exercise resume fail-fast path");

  assert_true(facade.stop(100U),
              "facade logging test should stop initialized facade");

  const auto& events = backend->events();
  const auto* init_event = find_event(
      events,
      "runtime.facade.init",
      {{"runtime_instance_id", "rt-facade-log"},
       {"accepted", "true"},
       {"readiness_level", "stub-ready"}});
  assert_true(init_event != nullptr,
              "agent facade should log init readiness outcome");

  const auto* handle_event = find_event(
      events,
      "runtime.facade.handle",
      {{"request_id", "req-facade-log"},
       {"session_id", "session-facade-log"},
       {"trace_id", "trace-facade-log"},
       {"result_status", "Completed"},
       {"outcome_reason", "completed"}});
  assert_true(handle_event != nullptr,
              "agent facade should log handle result facts");
  assert_true(handle_event->level == LogLevel::Info,
              "successful facade handle should emit info-level log");

  const auto* resume_event = find_event(
      events,
      "runtime.facade.resume",
      {{"request_id", "resume-facade-log"},
       {"result_status", "Failed"},
       {"outcome_reason", "missing_required_anchors"}});
  assert_true(resume_event != nullptr,
              "agent facade should log resume fail-fast reason");
  assert_true(resume_event->level == LogLevel::Error,
              "failed facade resume should emit error-level log");

  const auto* stop_event = find_event(
      events,
      "runtime.facade.stop",
      {{"runtime_instance_id", "rt-facade-log"},
       {"was_initialized", "true"},
       {"stopped", "true"}});
  assert_true(stop_event != nullptr,
              "agent facade should log stop outcome");

  for (const auto& event : events) {
    for (const auto& [key, value] : event.attrs) {
      (void)key;
      assert_true(value.find("must-not-appear") == std::string::npos,
                  "facade logs should not include request user_input");
    }
  }
}

void test_runtime_health_probe_emits_stateful_probe_logs() {
  RecordingDispatchBackend* backend = nullptr;
  const auto logger = make_logger(&backend);

  auto provider = std::make_shared<MutableHealthSignalProvider>(RuntimeHealthSample{
      .dependencies_ready = true,
      .watchdog_healthy = true,
      .telemetry_degraded = false,
      .event_bus_overflow = false,
      .maintenance_backlog = false,
      .safe_mode_active = false,
      .failed_components = {},
      .latency_ms = 12,
      .sampled_at_unix_ms = 1700000002000LL,
      .detail_ref = "status://runtime/health/healthy",
  });
  RuntimeHealthProbe probe(provider, RuntimeHealthProbeOptions{
                                      .detail_namespace = "status://runtime/health",
                                      .now_ms = []() { return 1700000002001LL; },
                                      .logger = logger,
                                      .runtime_instance_id = "runtime-owner-logging-test",
                                  });

  const auto healthy_result = probe.probe();
  const auto repeated_healthy_result = probe.probe();
  assert_true(healthy_result.status == dasall::infra::ProbeStatus::Healthy &&
                  repeated_healthy_result.status == dasall::infra::ProbeStatus::Healthy,
              "health probe logging test should observe healthy baseline twice");
  assert_true(count_events(backend->events(), "runtime.health.probe") == 1U,
              "health probe should suppress repeated healthy steady-state logs");

  provider->set_sample(RuntimeHealthSample{
      .dependencies_ready = true,
      .watchdog_healthy = true,
      .telemetry_degraded = true,
      .event_bus_overflow = true,
      .maintenance_backlog = false,
      .safe_mode_active = false,
      .failed_components = {"runtime.custom_dependency"},
      .latency_ms = 24,
      .sampled_at_unix_ms = 1700000002100LL,
      .detail_ref = "status://runtime/health/degraded",
  });
  const auto degraded_result = probe.probe();
  assert_true(degraded_result.status == dasall::infra::ProbeStatus::Degraded,
              "health probe logging test should observe degraded status");

  const auto* degraded_event = find_event(
      backend->events(),
      "runtime.health.probe",
      {{"runtime_instance_id", "runtime-owner-logging-test"},
       {"status", "Degraded"},
       {"readiness", "true"},
       {"degraded", "true"},
       {"failed_component_count", "3"}});
  assert_true(degraded_event != nullptr,
              "runtime health probe should log degraded probe facts");
  assert_true(degraded_event->level == LogLevel::Warn,
              "degraded health probe should emit warn-level log");
  assert_true(has_attr(*degraded_event, "latency_ms", "24"),
              "health probe log should expose latency budget evidence");
}

}  // namespace

int main() {
  try {
    test_recovery_manager_emits_structured_logs_without_leaking_retry_tokens();
    test_safe_mode_controller_emits_structured_logs_for_entry_and_exit();
    test_agent_facade_emits_entry_logs_without_user_input();
    test_runtime_health_probe_emits_stateful_probe_logs();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}