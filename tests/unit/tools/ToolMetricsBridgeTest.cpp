#include <algorithm>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "metrics/MetricTypes.h"
#include "metrics/IMetricsProvider.h"
#include "ops/ToolMetricsBridge.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

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
          "metrics://tools/recorded");
    }

    auto result = scripted_record_results.front();
    scripted_record_results.pop_front();
    return result;
  }

  std::deque<dasall::infra::metrics::MetricsOperationStatus> scripted_record_results;
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
        "metrics://tools/provider-init");
  }

  std::shared_ptr<dasall::infra::metrics::IMeter> get_meter(
      const dasall::infra::metrics::MeterScope& scope) override {
    last_scope = scope;
    ++get_meter_call_total;
    return meter_;
  }

  dasall::infra::metrics::MetricsOperationStatus force_flush(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://tools/provider-flush");
  }

  dasall::infra::metrics::MetricsOperationStatus shutdown(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://tools/provider-shutdown");
  }

  dasall::infra::metrics::MeterScope last_scope{};
  std::uint64_t get_meter_call_total = 0;

 private:
  std::shared_ptr<RecordingMeter> meter_;
};

[[nodiscard]] dasall::tools::ToolInvocationContext make_context() {
  return dasall::tools::ToolInvocationContext{
      .caller_domain = std::string("runtime.main"),
      .session_id = std::string("session-metrics"),
      .profile_snapshot = nullptr,
      .trace = {
          .trace_id = std::string("trace-metrics"),
          .span_id = std::string("span-metrics"),
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::nullopt,
  };
}

[[nodiscard]] dasall::contracts::ToolRequest make_request(std::string tool_name) {
  return dasall::contracts::ToolRequest{
      .request_id = std::string("req-metrics"),
      .tool_call_id = std::string("call-metrics"),
      .tool_name = std::move(tool_name),
      .invocation_kind = dasall::contracts::ToolInvocationKind::Action,
      .arguments_payload = std::string("{}"),
      .created_at = 1000,
      .goal_id = std::string("goal-metrics"),
      .worker_task_id = std::string("worker-metrics"),
      .runtime_budget = std::nullopt,
      .timeout_ms = 2500U,
      .idempotency_key = std::string("idem-metrics"),
      .tags = std::vector<std::string>{"tools", "metrics"},
  };
}

[[nodiscard]] dasall::contracts::ToolDescriptor make_descriptor(
    std::string tool_name,
    dasall::contracts::ToolCategory category) {
  return dasall::contracts::ToolDescriptor{
      .tool_name = std::move(tool_name),
      .display_name = std::string("Metrics Tool"),
      .category = category,
      .capability_tier = dasall::contracts::ToolCapabilityTier::Stable,
      .is_read_only = false,
      .supports_compensation = false,
      .default_timeout_ms = 2500U,
      .input_schema_ref = std::string("schema://tools/metrics/input"),
      .output_schema_ref = std::string("schema://tools/metrics/output"),
      .required_scopes = std::vector<std::string>{"tools.execute"},
      .tags = std::vector<std::string>{"builtin"},
      .version = std::string("1.0.0"),
  };
}

[[nodiscard]] dasall::tools::ToolInvocationEnvelope make_success_envelope() {
  return dasall::tools::ToolInvocationEnvelope{
      .tool_result = dasall::contracts::ToolResult{
          .request_id = std::string("req-metrics"),
          .tool_call_id = std::string("call-metrics"),
          .tool_name = std::string("agent.terminal"),
          .success = true,
          .payload = std::string("{\"stdout\":\"ok\"}"),
          .error = std::nullopt,
          .side_effects = std::nullopt,
          .completed_at = 2000,
          .duration_ms = 12,
          .goal_id = std::string("goal-metrics"),
          .worker_task_id = std::string("worker-metrics"),
          .tags = std::vector<std::string>{"tools", "metrics"},
      },
      .observation = std::nullopt,
      .observation_digest = std::nullopt,
      .route_facts = dasall::tools::ToolRouteFacts{
          .route_kind = std::string("builtin"),
          .route_ref = std::string("builtin"),
          .decision_reason = std::string("route.builtin.selected"),
          .plugin_id = std::nullopt,
          .server_id = std::nullopt,
      },
      .evidence_refs = std::vector<std::string>{"tool://call-metrics"},
      .compensation_hints = std::nullopt,
      .failure_reason_code = std::nullopt,
  };
}

[[nodiscard]] dasall::tools::ToolInvocationEnvelope make_partial_failure_envelope() {
  return dasall::tools::ToolInvocationEnvelope{
      .tool_result = dasall::contracts::ToolResult{
          .request_id = std::string("req-metrics"),
          .tool_call_id = std::string("call-metrics"),
          .tool_name = std::string("agent.terminal"),
          .success = false,
          .payload = std::nullopt,
          .error = dasall::contracts::ErrorInfo{
              .failure_type = dasall::contracts::classify_result_code(
                  dasall::contracts::ResultCode::ProviderTimeout),
              .retryable = true,
              .safe_to_replan = true,
              .details = dasall::contracts::ErrorDetails{
                  .code = static_cast<int>(dasall::contracts::ResultCode::ProviderTimeout),
                  .message = std::string("provider timeout"),
                  .stage = std::string("tools.manager.execute"),
              },
              .source_ref = dasall::contracts::ErrorSourceRefMinimal{
                  .ref_type = std::string("tool_manager"),
                  .ref_id = std::string("agent.terminal"),
              },
          },
          .side_effects = std::vector<std::string>{"safe_mode.partial"},
          .completed_at = 2100,
          .duration_ms = 21,
          .goal_id = std::string("goal-metrics"),
          .worker_task_id = std::string("worker-metrics"),
          .tags = std::vector<std::string>{"tools", "metrics"},
      },
      .observation = std::nullopt,
      .observation_digest = std::nullopt,
      .route_facts = dasall::tools::ToolRouteFacts{
          .route_kind = std::string("builtin"),
          .route_ref = std::string("builtin"),
          .decision_reason = std::string("route.builtin.selected"),
          .plugin_id = std::nullopt,
          .server_id = std::nullopt,
      },
      .evidence_refs = std::vector<std::string>{"tool://call-metrics"},
      .compensation_hints = std::nullopt,
      .failure_reason_code = std::string("tool.timeout"),
  };
}

void test_tool_metrics_bridge_emits_frozen_metric_families_and_samples() {
  using dasall::infra::metrics::MetricType;

  auto meter = std::make_shared<RecordingMeter>();
  auto provider = std::make_shared<RecordingMetricsProvider>(meter);
  dasall::tools::ops::ToolMetricsBridge bridge(
      provider,
      dasall::tools::ops::ToolMetricsBridgeOptions{
          .enabled = true,
          .profile_id = "desktop_full",
          .metrics_granularity = "full",
          .meter_scope_name = "tools",
          .meter_scope_version = "v1",
          .now_ms = []() { return 1712737000000LL; },
      });

  const auto request = make_request("agent.terminal");
  const auto descriptor = make_descriptor("agent.terminal", dasall::contracts::ToolCategory::Action);
  const auto context = make_context();

  const auto preflight_failure = bridge.record_preflight_failure(
      request,
      descriptor,
      dasall::tools::ToolInvocationEnvelope{
          .tool_result = std::nullopt,
          .observation = std::nullopt,
          .observation_digest = std::nullopt,
          .route_facts = std::nullopt,
          .evidence_refs = std::nullopt,
          .compensation_hints = std::nullopt,
          .failure_reason_code = std::string("tool.manager.validate"),
      },
      context);
  const auto admission_denied = bridge.record_admission_denied(
      request,
      descriptor,
      "policy.confirmation_required",
      context);
  const auto stale_route = bridge.record_route_selection(
      request,
      descriptor,
      dasall::tools::route::ToolRouteDecision{
          .available = true,
          .route = dasall::contracts::ToolIRRoute::MCPRemote,
          .lane_key = std::string("mcp"),
          .reason_code = std::string("route.mcp.selected"),
          .uses_stale_snapshot = true,
          .server_id = std::string("server-mcp-1"),
      },
      context);
  const auto terminal_success = bridge.record_execution_terminal(
      request,
      descriptor,
      make_success_envelope(),
      context);
  const auto partial_failure = bridge.record_execution_terminal(
      request,
      descriptor,
      make_partial_failure_envelope(),
      context);
  const auto workflow_failure = bridge.record_workflow_step_failure(
      "wf-safe-mode",
      "step-rollback",
      "provider_timeout",
      context);

  assert_true(preflight_failure.emitted && admission_denied.emitted && stale_route.emitted &&
                  terminal_success.emitted && partial_failure.emitted && workflow_failure.emitted,
              "tool metrics bridge should emit request/deny/stale/latency/partial/workflow samples through the metrics provider");
  assert_true(!bridge.is_degraded() && bridge.has_active_meter() && bridge.instruments_registered(),
              "successful tool metric emissions should keep the bridge healthy and register frozen instruments once");
  assert_equal(std::string("tools"),
               provider->last_scope.name,
               "tool metrics bridge should request the frozen tools meter scope");
  assert_equal(std::string("v1"),
               provider->last_scope.version,
               "tool metrics bridge should preserve the frozen tools meter scope version");
  assert_equal(6,
               static_cast<int>(meter->created_identities.size()),
               "tool metrics bridge should register the six frozen tools metric families on first use");
  assert_true(std::any_of(meter->created_identities.begin(),
                          meter->created_identities.end(),
                          [](const auto& identity) {
                            return identity.name == "tool_request_total" &&
                                   identity.type == MetricType::Counter && identity.unit == "1";
                          }) &&
                  std::any_of(meter->created_identities.begin(),
                              meter->created_identities.end(),
                              [](const auto& identity) {
                                return identity.name == "tool_execution_latency_ms" &&
                                       identity.type == MetricType::Histogram && identity.unit == "ms";
                              }) &&
                  std::any_of(meter->created_identities.begin(),
                              meter->created_identities.end(),
                              [](const auto& identity) {
                                return identity.name == "tool_workflow_step_failure_total" &&
                                       identity.type == MetricType::Counter && identity.unit == "1";
                              }),
              "tool metrics bridge should preserve the frozen name/type/unit contract for request, latency, and workflow failure families");
  assert_true(std::any_of(meter->recorded_samples.begin(),
                          meter->recorded_samples.end(),
                          [](const auto& sample) {
                            return sample.identity_ref.name == "tool_mcp_stale_snapshot_total" &&
                                   sample.labels.outcome == "degraded";
                          }) &&
                  std::any_of(meter->recorded_samples.begin(),
                              meter->recorded_samples.end(),
                              [](const auto& sample) {
                                return sample.identity_ref.name == "tool_partial_side_effect_total" &&
                                       sample.labels.outcome == "degraded";
                              }),
              "tool metrics bridge should export stale snapshot and partial side effect signals through the frozen metric names");
}

void test_tool_metrics_bridge_degrades_when_meter_record_fails() {
  using dasall::contracts::ResultCode;
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::infra::metrics::MetricsOperationStatus;

  auto meter = std::make_shared<RecordingMeter>();
  meter->scripted_record_results.push_back(MetricsOperationStatus::failure(
      ResultCode::ProviderTimeout,
      "metrics exporter timed out",
      "metrics.record",
      "RecordingMeter"));

  auto provider = std::make_shared<RecordingMetricsProvider>(meter);
  dasall::tools::ops::ToolMetricsBridge bridge(
      provider,
      dasall::tools::ops::ToolMetricsBridgeOptions{
          .enabled = true,
          .profile_id = "desktop_full",
          .metrics_granularity = "full",
          .meter_scope_name = "tools",
          .meter_scope_version = "v1",
          .now_ms = []() { return 1712737001000LL; },
      });

  const auto result = bridge.record_execution_terminal(
      make_request("agent.terminal"),
      make_descriptor("agent.terminal", dasall::contracts::ToolCategory::Action),
      make_success_envelope(),
      make_context());

  assert_true(!result.emitted && result.has_consistent_state(),
              "meter record failures should return a failed tool metrics emit result instead of pretending the sample was exported");
  assert_true(result.bridge_degraded,
              "provider/exporter failures should mark the tool metrics bridge as degraded");
  assert_true(result.metrics_error_code == MetricsErrorCode::ExportFailure,
              "provider timeout should normalize to MET_E_EXPORT_FAILURE for tool bridge diagnostics");
  assert_true(bridge.is_degraded() && bridge.emission_failure_total() == 1U,
              "tool metrics bridge should retain degraded status and failure count after exporter failure");
}

void test_tool_metrics_bridge_treats_disabled_observability_as_noop() {
  dasall::tools::ops::ToolMetricsBridge bridge(
      nullptr,
      dasall::tools::ops::ToolMetricsBridgeOptions{
          .enabled = false,
          .profile_id = "edge_minimal",
          .metrics_granularity = "minimal",
          .meter_scope_name = "tools",
          .meter_scope_version = "v1",
          .now_ms = []() { return 1712737002000LL; },
      });

  const auto result = bridge.record_execution_terminal(
      make_request("agent.terminal"),
      make_descriptor("agent.terminal", dasall::contracts::ToolCategory::Action),
      make_success_envelope(),
      make_context());

  assert_true(!result.emitted && result.signal_suppressed && result.has_consistent_state(),
              "disabled observability should suppress tool metric emission as an explicit no-op");
  assert_true(!bridge.has_active_meter() && !bridge.instruments_registered() && !bridge.is_degraded(),
              "disabled observability should not acquire a meter or degrade the bridge");
}

}  // namespace

int main() {
  try {
    test_tool_metrics_bridge_emits_frozen_metric_families_and_samples();
    test_tool_metrics_bridge_degrades_when_meter_record_fails();
    test_tool_metrics_bridge_treats_disabled_observability_as_noop();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}