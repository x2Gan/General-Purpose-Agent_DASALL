#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "AgentFacade.h"
#include "RuntimeDependencySet.h"
#include "RuntimePolicySnapshot.h"
#include "audit/IAuditLogger.h"
#include "budget/BudgetDecision.h"
#include "checkpoint/RecoveryOutcome.h"
#include "metrics/IMeter.h"
#include "metrics/IMetricsProvider.h"
#include "metrics/MetricTypes.h"
#include "support/TestAssertions.h"
#include "telemetry/RuntimeEventBus.h"
#include "telemetry/RuntimeTelemetryBridge.h"

namespace {

template <typename T>
std::shared_ptr<T> make_live_port() {
  auto holder = std::make_shared<int>(1);
  return std::shared_ptr<T>(holder, reinterpret_cast<T*>(holder.get()));
}

class RecordingAuditLogger final : public dasall::infra::audit::IAuditLogger {
 public:
  dasall::infra::AuditWriteOutcome write_audit(
      const dasall::infra::AuditEvent& event,
      const dasall::infra::AuditContext& context) override {
    events.push_back(event);
    contexts.push_back(context);
    return dasall::infra::AuditWriteOutcome{
        .accepted = true,
        .persisted = true,
        .fallback_used = false,
        .error_code = std::nullopt,
    };
  }

  dasall::infra::ExportResult export_audit(const dasall::infra::ExportQuery&) override {
    return dasall::infra::ExportResult{};
  }

  std::vector<dasall::infra::AuditEvent> events;
  std::vector<dasall::infra::AuditContext> contexts;
};

class RecordingMeter final : public dasall::infra::metrics::IMeter {
 public:
  std::optional<dasall::infra::metrics::InstrumentHandle> create_counter(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_counters.push_back(identity.name);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":counter",
    };
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_gauge(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":gauge",
    };
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_histogram(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":histogram",
    };
  }

  dasall::infra::metrics::MetricsOperationStatus record(
      const dasall::infra::metrics::MetricSample& sample) override {
    samples.push_back(sample);
    return dasall::infra::metrics::MetricsOperationStatus::success("metrics://runtime-sink");
  }

  std::vector<std::string> created_counters;
  std::vector<dasall::infra::metrics::MetricSample> samples;
};

class RecordingMetricsProvider final : public dasall::infra::metrics::IMetricsProvider {
 public:
  explicit RecordingMetricsProvider(std::shared_ptr<RecordingMeter> meter)
      : meter_(std::move(meter)) {}

  dasall::infra::metrics::MetricsOperationStatus init(
      const dasall::infra::metrics::MetricsProviderConfig&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://runtime-sink-init");
  }

  std::shared_ptr<dasall::infra::metrics::IMeter> get_meter(
      const dasall::infra::metrics::MeterScope&) override {
    return meter_;
  }

  dasall::infra::metrics::MetricsOperationStatus force_flush(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://runtime-sink-flush");
  }

  dasall::infra::metrics::MetricsOperationStatus shutdown(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://runtime-sink-shutdown");
  }

 private:
  std::shared_ptr<RecordingMeter> meter_;
};

[[nodiscard]] bool contains_value(const std::vector<std::string>& values,
                                  const std::string& expected) {
  return std::find(values.begin(), values.end(), expected) != values.end();
}

[[nodiscard]] std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot>
make_policy_snapshot(const dasall::profiles::RuntimeSinkPolicy& sink_policy) {
  using dasall::profiles::CapabilityCachePolicy;
  using dasall::profiles::DegradePolicy;
  using dasall::profiles::ExecutionPolicy;
  using dasall::profiles::MemoryMaintenancePolicy;
  using dasall::profiles::ModelProfile;
  using dasall::profiles::ModelRoutePolicy;
  using dasall::profiles::OpsPolicy;
  using dasall::profiles::PromptPolicy;
  using dasall::profiles::RuntimePolicySnapshot;
  using dasall::profiles::TimeoutBudget;
  using dasall::profiles::TimeoutPolicy;
  using dasall::profiles::TokenBudgetPolicy;

  return std::make_shared<RuntimePolicySnapshot>(
      31U,
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
          .fallback_chain = {"local", "safe_mode"},
          .allow_model_failover = true,
          .allow_budget_degrade = true,
      },
      TimeoutPolicy{
          .llm = TimeoutBudget{.timeout_ms = 1500, .retry_budget = 1U, .circuit_breaker_threshold = 3U},
          .tool = TimeoutBudget{.timeout_ms = 1200, .retry_budget = 1U, .circuit_breaker_threshold = 3U},
          .mcp = TimeoutBudget{.timeout_ms = 1800, .retry_budget = 2U, .circuit_breaker_threshold = 3U},
          .workflow = TimeoutBudget{.timeout_ms = 2500, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
      },
      ExecutionPolicy{
          .requires_high_risk_confirmation = true,
          .safe_mode_enabled = true,
          .audit_level = "strict",
          .allowed_tool_domains = {"default"},
      },
      OpsPolicy{
          .log_level = "info",
          .metrics_granularity = "full",
          .trace_sample_ratio = 0.5,
          .remote_diagnostics_enabled = false,
          .upgrade_strategy = "manual",
      },
      1U,
      false,
      MemoryMaintenancePolicy{
          .enabled = true,
          .interval_ms = 45000,
          .jitter_ms = 5000,
          .retention_ms = 240000,
          .checkpoint_strategy = "passive_each_tick",
      },
      sink_policy);
}

[[nodiscard]] std::shared_ptr<dasall::runtime::RuntimeDependencySet> make_dependency_set() {
  auto dependency_set = std::make_shared<dasall::runtime::RuntimeDependencySet>();
  dependency_set->memory_manager = make_live_port<dasall::memory::IMemoryManager>();
  dependency_set->cognition_engine = make_live_port<dasall::cognition::ICognitionEngine>();
  dependency_set->response_builder = make_live_port<dasall::cognition::IResponseBuilder>();
  dependency_set->tool_manager = make_live_port<dasall::tools::IToolManager>();
  dependency_set->knowledge_service = make_live_port<dasall::knowledge::IKnowledgeService>();
  dependency_set->llm_manager = make_live_port<dasall::llm::ILLMManager>();
  dependency_set->runtime_event_bus = std::make_shared<dasall::runtime::RuntimeEventBus>(
      dasall::runtime::RuntimeEventBusOptions{.max_non_audit_queue_depth = 32U});
  dependency_set->runtime_telemetry_bridge =
      std::make_shared<dasall::runtime::RuntimeTelemetryBridge>(
          dependency_set->runtime_event_bus,
          dasall::runtime::RuntimeTelemetryBridgeOptions{
              .runtime_instance_id = "runtime.sink.unit",
              .now_ms = []() { return 1712400001000LL; },
          });
  return dependency_set;
}

[[nodiscard]] dasall::runtime::AgentInitRequest make_init_request(
    std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot> snapshot,
    std::shared_ptr<dasall::runtime::RuntimeDependencySet> dependency_set) {
  return dasall::runtime::AgentInitRequest{
      .runtime_instance_id = "runtime.sink.unit",
      .profile_id = "desktop_full",
      .policy_snapshot = std::move(snapshot),
      .dependency_set = std::move(dependency_set),
      .cold_start = true,
  };
}

void test_runtime_audit_sink_forwarding() {
  using dasall::contracts::RecoveryOutcome;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto dependency_set = make_dependency_set();
  auto audit_logger = std::make_shared<RecordingAuditLogger>();
  dependency_set->audit_logger = audit_logger;

  dasall::runtime::AgentFacade facade;
  const auto init_result = facade.init(make_init_request(
      make_policy_snapshot(dasall::profiles::RuntimeSinkPolicy{
          .fail_closed_on_audit_failure = true,
          .drop_on_metrics_failure = true,
      }),
      dependency_set));
  assert_true(init_result.accepted,
              "runtime audit sink forwarding should initialize facade when mandatory audit sink is present");

  const auto record = dependency_set->runtime_telemetry_bridge->emit_recovery_reject(
      RecoveryOutcome{
          .executed_action = std::string("abort_safe"),
          .final_runtime_state = std::string("FailedSafe"),
          .updated_retry_count = 2U,
          .checkpoint_ref = std::string("checkpoint-runtime-audit-001"),
          .compensation_result_ref = std::nullopt,
          .rejection_reason = std::string("retry budget exhausted"),
          .escalation_reason = std::nullopt,
      },
      dasall::runtime::RuntimeTelemetryContext{
          .request_id = std::string("req-runtime-audit-001"),
          .session_id = std::string("session-runtime-audit-001"),
          .trace_id = std::string("trace-runtime-audit-001"),
          .turn_id = std::string("turn-runtime-audit-001"),
          .checkpoint_id = std::string("chk-runtime-audit-001"),
      },
      "recovery_detail_secret=hidden");

  assert_equal(1,
               static_cast<int>(dependency_set->runtime_event_bus->dispatch_pending()),
               "runtime audit sink forwarding should dispatch exactly one runtime event");
  assert_equal(1,
               static_cast<int>(audit_logger->events.size()),
               "runtime audit sink forwarding should persist one audit event");
  assert_true(record.envelope.event_name == "runtime.recovery.reject",
              "runtime audit sink forwarding should emit the recovery reject event class");
  assert_true(audit_logger->events.front().action == "recovery_reject" &&
                  audit_logger->events.front().evidence_ref.ref ==
                      "checkpoint-runtime-audit-001",
              "runtime audit sink forwarding should map recovery rejects to audit action and checkpoint evidence");
  assert_true(audit_logger->contexts.front().request_id == "req-runtime-audit-001" &&
                  audit_logger->contexts.front().trace_id == "trace-runtime-audit-001",
              "runtime audit sink forwarding should preserve request and trace correlation in audit context");
}

void test_runtime_metrics_sink_forwarding() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto dependency_set = make_dependency_set();
  auto meter = std::make_shared<RecordingMeter>();
  dependency_set->metrics_provider = std::make_shared<RecordingMetricsProvider>(meter);

  dasall::runtime::AgentFacade facade;
  const auto init_result = facade.init(make_init_request(
      make_policy_snapshot(dasall::profiles::RuntimeSinkPolicy{
          .fail_closed_on_audit_failure = false,
          .drop_on_metrics_failure = false,
      }),
      dependency_set));
  assert_true(init_result.accepted,
              "runtime metrics sink forwarding should initialize facade when mandatory metrics sink is present");

  const auto record = dependency_set->runtime_telemetry_bridge->emit_budget_reject(
      dasall::runtime::make_budget_rejected_decision(
          dasall::runtime::BudgetViolationClass::LatencyExhausted,
          "budget rejected for metrics forwarding"),
      dasall::runtime::RuntimeTelemetryContext{
          .request_id = std::string("req-runtime-metrics-001"),
          .session_id = std::string("session-runtime-metrics-001"),
          .trace_id = std::string("trace-runtime-metrics-001"),
          .turn_id = std::string("turn-runtime-metrics-001"),
          .checkpoint_id = std::string("chk-runtime-metrics-001"),
      });

  assert_equal(1,
               static_cast<int>(dependency_set->runtime_event_bus->dispatch_pending()),
               "runtime metrics sink forwarding should dispatch exactly one runtime event");
  assert_equal(1,
               static_cast<int>(meter->samples.size()),
               "runtime metrics sink forwarding should record one metrics sample");
  assert_true(record.envelope.event_name == "runtime.budget.reject",
              "runtime metrics sink forwarding should emit the budget reject event class");
  assert_true(contains_value(meter->created_counters, "runtime_control_plane_event_total"),
              "runtime metrics sink forwarding should register the control-plane counter identity once");
  assert_true(meter->samples.front().identity_ref.name == "runtime_control_plane_event_total" &&
                  meter->samples.front().labels.stage == "budget_reject" &&
                  meter->samples.front().labels.profile == "desktop_full" &&
                  meter->samples.front().labels.outcome == "failure",
              "runtime metrics sink forwarding should project the runtime event into the frozen metrics label surface");
}

void test_runtime_sink_fail_closed_when_mandatory_sinks_are_missing() {
  using dasall::tests::support::assert_true;

  auto dependency_set = make_dependency_set();

  dasall::runtime::AgentFacade facade;
  const auto init_result = facade.init(make_init_request(
      make_policy_snapshot(dasall::profiles::RuntimeSinkPolicy{
          .fail_closed_on_audit_failure = true,
          .drop_on_metrics_failure = false,
      }),
      dependency_set));

  assert_true(!init_result.accepted,
              "runtime sink fail-closed should reject init when mandatory audit and metrics sinks are absent");
  assert_true(contains_value(init_result.missing_required_ports, "audit") &&
                  contains_value(init_result.missing_required_ports, "metrics"),
              "runtime sink fail-closed should surface both audit and metrics as missing required ports");
  assert_true(init_result.health_summary.find("missing required dependency ports") !=
                  std::string::npos,
              "runtime sink fail-closed should keep the init failure rooted in required dependency readiness");
}

}  // namespace

int main() {
  try {
    test_runtime_audit_sink_forwarding();
    test_runtime_metrics_sink_forwarding();
    test_runtime_sink_fail_closed_when_mandatory_sinks_are_missing();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}