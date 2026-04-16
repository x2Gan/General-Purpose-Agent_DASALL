#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "RuntimePolicySnapshot.h"
#include "ToolManager.h"
#include "audit/IAuditLogger.h"
#include "metrics/MetricTypes.h"
#include "metrics/IMetricsProvider.h"
#include "ops/ToolAuditBridge.h"
#include "ops/ToolMetricsBridge.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class ScriptedAuditLogger final : public dasall::infra::audit::IAuditLogger {
 public:
  dasall::infra::AuditWriteOutcome write_audit(
      const dasall::infra::AuditEvent& event,
      const dasall::infra::AuditContext& context) override {
    events.push_back(event);
    contexts.push_back(context);
    if (!scripted_outcomes.empty()) {
      const auto outcome = scripted_outcomes.front();
      scripted_outcomes.erase(scripted_outcomes.begin());
      return outcome;
    }

    return dasall::infra::AuditWriteOutcome{
        .accepted = true,
        .persisted = true,
        .fallback_used = false,
        .error_code = std::nullopt,
    };
  }

  dasall::infra::ExportResult export_audit(
      const dasall::infra::ExportQuery&) override {
    return dasall::infra::ExportResult{};
  }

  std::vector<dasall::infra::AuditWriteOutcome> scripted_outcomes;
  std::vector<dasall::infra::AuditEvent> events;
  std::vector<dasall::infra::AuditContext> contexts;
};

class RecordingMeter final : public dasall::infra::metrics::IMeter {
 public:
    std::optional<dasall::infra::metrics::InstrumentHandle> create_counter(
            const dasall::infra::metrics::MetricIdentity& identity) override {
        created_identities.push_back(identity);
        return dasall::infra::metrics::InstrumentHandle{
                .instrument_key = identity.name + ":counter",
        };
    }

    std::optional<dasall::infra::metrics::InstrumentHandle> create_gauge(
            const dasall::infra::metrics::MetricIdentity& identity) override {
        created_identities.push_back(identity);
        return dasall::infra::metrics::InstrumentHandle{
                .instrument_key = identity.name + ":gauge",
        };
    }

    std::optional<dasall::infra::metrics::InstrumentHandle> create_histogram(
            const dasall::infra::metrics::MetricIdentity& identity) override {
        created_identities.push_back(identity);
        return dasall::infra::metrics::InstrumentHandle{
                .instrument_key = identity.name + ":histogram",
        };
    }

    dasall::infra::metrics::MetricsOperationStatus record(
            const dasall::infra::metrics::MetricSample& sample) override {
        recorded_samples.push_back(sample);
        if (scripted_record_results.empty()) {
            return dasall::infra::metrics::MetricsOperationStatus::success(
                    "metrics://tools/integration-recorded");
        }

        auto result = scripted_record_results.front();
        scripted_record_results.erase(scripted_record_results.begin());
        return result;
    }

    std::vector<dasall::infra::metrics::MetricsOperationStatus> scripted_record_results;
    std::vector<dasall::infra::metrics::MetricIdentity> created_identities;
    std::vector<dasall::infra::metrics::MetricSample> recorded_samples;
};

class RecordingMetricsProvider final : public dasall::infra::metrics::IMetricsProvider {
 public:
    explicit RecordingMetricsProvider(std::shared_ptr<RecordingMeter> meter)
            : meter_(std::move(meter)) {}

    dasall::infra::metrics::MetricsOperationStatus init(
            const dasall::infra::metrics::MetricsProviderConfig&) override {
        return dasall::infra::metrics::MetricsOperationStatus::success(
                "metrics://tools/integration-provider-init");
    }

    std::shared_ptr<dasall::infra::metrics::IMeter> get_meter(
            const dasall::infra::metrics::MeterScope& scope) override {
        last_scope = scope;
        return meter_;
    }

    dasall::infra::metrics::MetricsOperationStatus force_flush(
            const dasall::infra::metrics::MetricsCallDeadline&) override {
        return dasall::infra::metrics::MetricsOperationStatus::success(
                "metrics://tools/integration-provider-flush");
    }

    dasall::infra::metrics::MetricsOperationStatus shutdown(
            const dasall::infra::metrics::MetricsCallDeadline&) override {
        return dasall::infra::metrics::MetricsOperationStatus::success(
                "metrics://tools/integration-provider-shutdown");
    }

    dasall::infra::metrics::MeterScope last_scope{};

 private:
    std::shared_ptr<RecordingMeter> meter_;
};

[[nodiscard]] bool has_action(const std::vector<dasall::infra::AuditEvent>& events,
                              const std::string& action) {
  return std::find_if(events.begin(), events.end(), [&](const auto& event) {
           return event.action == action;
         }) != events.end();
}

[[nodiscard]] bool contains_string(const std::vector<std::string>& values,
                                   const std::string& expected) {
  return std::find(values.begin(), values.end(), expected) != values.end();
}

[[nodiscard]] bool has_metric_sample(
        const std::vector<dasall::infra::metrics::MetricSample>& samples,
        const std::string& metric_name,
        const std::string& outcome) {
    return std::find_if(samples.begin(), samples.end(), [&](const auto& sample) {
                     return sample.identity_ref.name == metric_name &&
                                    sample.labels.outcome == outcome;
                 }) != samples.end();
}

[[nodiscard]] dasall::profiles::RuntimePolicySnapshot make_snapshot() {
  return dasall::profiles::RuntimePolicySnapshot{
      1U,
      "desktop_full",
      dasall::contracts::RuntimeBudget{
          .max_tokens = 4096U,
          .max_turns = 8U,
          .max_tool_calls = 24U,
          .max_latency_ms = 8000U,
          .max_replan_count = 2U,
      },
      dasall::profiles::ModelProfile{
          .stage_routes = {{
              "planner",
              dasall::profiles::ModelRoutePolicy{
                  .route = "local.small",
                  .fallback_route = std::string("builtin_only"),
                  .streaming_enabled = false,
              },
          }},
      },
      dasall::profiles::TokenBudgetPolicy{
          .max_input_tokens = 1024U,
          .max_output_tokens = 512U,
          .max_history_turns = 4U,
          .compression_threshold = 768U,
      },
      dasall::profiles::PromptPolicy{
          .allowed_prompt_releases = {"stable"},
          .trusted_sources = {"profiles"},
          .tool_visibility_rules = {"builtin:all"},
      },
      dasall::profiles::CapabilityCachePolicy{
          .refresh_interval_ms = 10000,
          .expire_after_ms = 180000,
          .stale_read_allowed = false,
          .failure_backoff_ms = 5000,
      },
      dasall::profiles::DegradePolicy{
          .fallback_chain = {"builtin_only"},
          .allow_model_failover = false,
          .allow_budget_degrade = true,
      },
      dasall::profiles::TimeoutPolicy{
          .llm = dasall::profiles::TimeoutBudget{
              .timeout_ms = 1800,
              .retry_budget = 0U,
              .circuit_breaker_threshold = 3U,
          },
          .tool = dasall::profiles::TimeoutBudget{
              .timeout_ms = 2500,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 4U,
          },
          .mcp = dasall::profiles::TimeoutBudget{
              .timeout_ms = 2000,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 3U,
          },
          .workflow = dasall::profiles::TimeoutBudget{
              .timeout_ms = 5000,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 3U,
          },
      },
      dasall::profiles::ExecutionPolicy{
          .requires_high_risk_confirmation = true,
          .safe_mode_enabled = true,
          .audit_level = "full",
          .allowed_tool_domains = {"builtin"},
      },
      dasall::profiles::OpsPolicy{
          .log_level = "info",
          .metrics_granularity = "full",
          .trace_sample_ratio = 0.5,
          .remote_diagnostics_enabled = false,
          .upgrade_strategy = "rolling",
      },
      4U};
}

[[nodiscard]] dasall::contracts::ToolRequest make_success_request() {
  return dasall::contracts::ToolRequest{
      .request_id = std::string("req-audit-int-success"),
      .tool_call_id = std::string("call-audit-int-success"),
      .tool_name = std::string("agent.terminal"),
      .invocation_kind = dasall::contracts::ToolInvocationKind::Action,
      .arguments_payload = std::string("{\"command\":\"echo integration\"}"),
      .created_at = 1000,
      .goal_id = std::string("goal-audit-int"),
      .worker_task_id = std::string("worker-audit-int"),
      .runtime_budget = std::nullopt,
      .timeout_ms = 2500U,
      .idempotency_key = std::string("idem-audit-int-success"),
      .tags = std::vector<std::string>{"integration", "tools"},
  };
}

[[nodiscard]] dasall::contracts::ToolRequest make_failure_request() {
  return dasall::contracts::ToolRequest{
      .request_id = std::string("req-audit-int-failed"),
      .tool_call_id = std::string("call-audit-int-failed"),
      .tool_name = std::string("tool.missing"),
      .invocation_kind = dasall::contracts::ToolInvocationKind::Action,
      .arguments_payload = std::string("{}"),
      .created_at = 1001,
      .goal_id = std::string("goal-audit-int"),
      .worker_task_id = std::string("worker-audit-int"),
      .runtime_budget = std::nullopt,
      .timeout_ms = 2500U,
      .idempotency_key = std::string("idem-audit-int-failed"),
      .tags = std::vector<std::string>{"integration", "tools"},
  };
}

[[nodiscard]] dasall::tools::ToolInvocationContext make_context() {
  const auto snapshot = make_snapshot();
  return dasall::tools::ToolInvocationContext{
      .caller_domain = std::string("runtime.main"),
      .session_id = std::string("session-audit-int"),
      .profile_snapshot = nullptr,
      .trace = {
          .trace_id = std::string("trace-audit-int"),
          .span_id = std::string("span-audit-int"),
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::vector<dasall::tools::ToolConfirmationFact>{
          dasall::tools::ToolConfirmationFact{
              .confirmation_id = std::string("confirm-audit-int"),
              .subject_ref = std::string("goal://audit-int"),
              .proof_type = std::string("user.approved"),
              .confirmed_at_ms = 900,
          }},
  };
}

[[nodiscard]] dasall::tools::ToolInvocationContext make_bound_context(
    const dasall::profiles::RuntimePolicySnapshot& snapshot) {
  auto context = make_context();
  context.profile_snapshot = &snapshot;
  return context;
}

[[nodiscard]] dasall::tools::ToolInvocationContext make_denied_context(
        const dasall::profiles::RuntimePolicySnapshot& snapshot) {
    auto context = make_bound_context(snapshot);
    context.confirmation_facts = std::nullopt;
    return context;
}

void test_tool_observability_integration_emits_audit_events_for_success_failure_and_compensation() {
  ScriptedAuditLogger audit_logger;
  auto audit_bridge = std::make_shared<dasall::tools::ops::ToolAuditBridge>(&audit_logger);
  auto meter = std::make_shared<RecordingMeter>();
  auto metrics_provider = std::make_shared<RecordingMetricsProvider>(meter);
  auto metrics_bridge = std::make_shared<dasall::tools::ops::ToolMetricsBridge>(
      metrics_provider,
      dasall::tools::ops::ToolMetricsBridgeOptions{
          .enabled = true,
          .profile_id = "desktop_full",
          .metrics_granularity = "full",
          .meter_scope_name = "tools",
          .meter_scope_version = "v1",
          .now_ms = []() { return 1712738000000LL; },
      });

  dasall::tools::manager::ToolManagerDependencies dependencies;
  dependencies.audit_hooks = dasall::tools::ops::ToolAuditBridge::bind_hooks(audit_bridge);
  dependencies.metrics_bridge = metrics_bridge;

  dasall::tools::ToolManager manager(std::move(dependencies));
  const auto snapshot = make_snapshot();
  const auto context = make_bound_context(snapshot);
    const auto denied_context = make_denied_context(snapshot);

  const auto success = manager.invoke(make_success_request(), context);
  const auto failed = manager.invoke(make_failure_request(), context);
    const auto denied = manager.invoke(make_success_request(), denied_context);
  const auto compensation = manager.compensate(
      dasall::tools::CompensationRequest{
          .tool_call_id = std::string("call-audit-int-success"),
          .compensation_action = std::string("safe_mode.exit"),
          .target_ref = std::string("goal://audit-int"),
          .reason_code = std::string("manual_recovery"),
          .evidence_refs = std::vector<std::string>{"recovery://call-audit-int-success"},
      },
      context);

  assert_true(success.tool_result.has_value() && success.tool_result->success.value_or(false),
              "tools observability integration should not change the successful builtin result");
  assert_true(failed.tool_result.has_value() && !failed.tool_result->success.value_or(true),
              "tools observability integration should keep the fail-closed execution result intact");
    assert_true(denied.tool_result.has_value() && !denied.tool_result->success.value_or(true),
                            "tools observability integration should preserve policy-denied results while emitting denied-path observability signals");
  assert_true(compensation.tool_result.has_value() &&
                  !compensation.tool_result->success.value_or(true),
              "tools observability integration should expose the current unconfigured compensation result unchanged");
  assert_true(has_action(audit_logger.events, "tool.execution.requested") &&
                  has_action(audit_logger.events, "tool.execution.completed") &&
                  has_action(audit_logger.events, "tool.execution.failed") &&
                  has_action(audit_logger.events, "tool.compensation.executed"),
              "tools observability integration should emit requested/completed/failed/compensation audit events through ToolManager hooks");
  assert_equal(std::string("tools.execution"),
               audit_logger.contexts.front().worker_type,
               "tools observability integration should keep audit events on the dedicated tools.execution worker type");
  assert_true(contains_string(audit_logger.events.front().side_effects,
                              "caller_domain:runtime.main") &&
                  contains_string(audit_logger.events.back().side_effects,
                                  "compensation_action:safe_mode.exit"),
              "tools observability integration should preserve caller and compensation facts in emitted audit payloads");
    assert_true(has_metric_sample(meter->recorded_samples, "tool_request_total", "success") &&
                                    has_metric_sample(meter->recorded_samples, "tool_request_total", "failure") &&
                                    has_metric_sample(meter->recorded_samples, "tool_admission_denied_total", "rejected") &&
                                    has_metric_sample(meter->recorded_samples, "tool_execution_latency_ms", "success"),
                            "tools observability integration should emit request, denied, and latency metric samples alongside audit events");
    assert_equal(std::string("tools"),
                             metrics_provider->last_scope.name,
                             "tools observability integration should request the frozen tools meter scope");
  for (const auto& event : audit_logger.events) {
    assert_true(!contains_string(event.side_effects,
                                 std::string("{\"command\":\"echo integration\"}")) &&
                    !contains_string(event.side_effects,
                                     std::string("{\"status\":\"executed\",\"route\":\"builtin\",\"lane\":\"builtin\"}")),
                "tools observability integration should never leak raw request or result payloads into audit side effects");
  }
  assert_true(audit_bridge->get_status().emitted_total >= 4,
              "tools observability integration should track persisted audit emissions in bridge status");
    assert_true(!metrics_bridge->is_degraded() && metrics_bridge->emission_attempt_total() >= 3,
                            "tools observability integration should keep the metrics bridge healthy on the success/failure main paths");
}

void test_tool_observability_integration_keeps_main_result_when_audit_sink_fails() {
  ScriptedAuditLogger audit_logger;
  audit_logger.scripted_outcomes = {
      dasall::infra::AuditWriteOutcome{
          .accepted = false,
          .persisted = false,
          .fallback_used = false,
          .error_code = dasall::contracts::ResultCode::RuntimeRetryExhausted,
      },
      dasall::infra::AuditWriteOutcome{
          .accepted = false,
          .persisted = false,
          .fallback_used = false,
          .error_code = dasall::contracts::ResultCode::RuntimeRetryExhausted,
      },
  };
  auto audit_bridge = std::make_shared<dasall::tools::ops::ToolAuditBridge>(&audit_logger);
  auto meter = std::make_shared<RecordingMeter>();
  meter->scripted_record_results = {
      dasall::infra::metrics::MetricsOperationStatus::failure(
          dasall::contracts::ResultCode::ProviderTimeout,
          "metrics exporter timed out",
          "metrics.record",
          "RecordingMeter")};
  auto metrics_provider = std::make_shared<RecordingMetricsProvider>(meter);
  auto metrics_bridge = std::make_shared<dasall::tools::ops::ToolMetricsBridge>(
      metrics_provider,
      dasall::tools::ops::ToolMetricsBridgeOptions{
          .enabled = true,
          .profile_id = "desktop_full",
          .metrics_granularity = "full",
          .meter_scope_name = "tools",
          .meter_scope_version = "v1",
          .now_ms = []() { return 1712738001000LL; },
      });

  dasall::tools::manager::ToolManagerDependencies dependencies;
  dependencies.audit_hooks = dasall::tools::ops::ToolAuditBridge::bind_hooks(audit_bridge);
  dependencies.metrics_bridge = metrics_bridge;

  dasall::tools::ToolManager manager(std::move(dependencies));
  const auto snapshot = make_snapshot();
  const auto context = make_bound_context(snapshot);
  const auto success = manager.invoke(make_success_request(), context);

  assert_true(success.tool_result.has_value() && success.tool_result->success.value_or(false),
              "failing audit sink should not suppress the already-produced successful tool result");
  assert_true(audit_bridge->get_status().degraded &&
                  audit_bridge->get_status().emit_failures >= 2,
              "failing audit sink should remain observable through ToolAuditBridge status without blocking the main result");
    assert_true(metrics_bridge->is_degraded() && metrics_bridge->emission_failure_total() == 1U,
                            "failing metrics backend should remain observable through ToolMetricsBridge status without blocking the main result");
}

}  // namespace

int main() {
  try {
    test_tool_observability_integration_emits_audit_events_for_success_failure_and_compensation();
    test_tool_observability_integration_keeps_main_result_when_audit_sink_fails();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}