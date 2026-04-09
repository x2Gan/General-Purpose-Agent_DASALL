#include <algorithm>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "bridges/ServiceMetricsBridge.h"
#include "metrics/MetricTypes.h"
#include "support/TestAssertions.h"

namespace {

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
          "metrics://services/recorded");
    }

    auto result = scripted_record_results.front();
    scripted_record_results.pop_front();
    return result;
  }

  std::deque<dasall::infra::metrics::MetricsOperationStatus> scripted_record_results;
  std::vector<dasall::infra::metrics::MetricIdentity> created_identities;
  std::vector<dasall::infra::metrics::MetricSample> recorded_samples;
};

class RecordingMetricsProvider final
    : public dasall::infra::metrics::IMetricsProvider {
 public:
  explicit RecordingMetricsProvider(std::shared_ptr<RecordingMeter> meter)
      : meter_(std::move(meter)) {}

  dasall::infra::metrics::MetricsOperationStatus init(
      const dasall::infra::metrics::MetricsProviderConfig&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://services/provider-init");
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
        "metrics://services/provider-flush");
  }

  dasall::infra::metrics::MetricsOperationStatus shutdown(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://services/provider-shutdown");
  }

  dasall::infra::metrics::MeterScope last_scope{};
  std::uint64_t get_meter_call_total = 0;

 private:
  std::shared_ptr<RecordingMeter> meter_;
};

[[nodiscard]] std::optional<dasall::infra::metrics::MetricSample> find_sample(
    const std::vector<dasall::infra::metrics::MetricSample>& samples,
    const std::string& name,
    const std::string& stage) {
  const auto it = std::find_if(samples.begin(), samples.end(), [&](const auto& sample) {
    return sample.identity_ref.name == name && sample.labels.stage == stage;
  });

  if (it == samples.end()) {
    return std::nullopt;
  }

  return *it;
}

[[nodiscard]] dasall::contracts::ErrorInfo make_error_info(
    dasall::contracts::ResultCodeCategory category,
    std::string message,
    std::string stage,
    std::string ref_id,
    bool retryable = false) {
  return dasall::contracts::ErrorInfo{
      .failure_type = category,
      .retryable = retryable,
      .safe_to_replan = false,
      .details = dasall::contracts::ErrorDetails{
          .code = 0,
          .message = std::move(message),
          .stage = std::move(stage),
      },
      .source_ref = dasall::contracts::ErrorSourceRefMinimal{
          .ref_type = "services",
          .ref_id = std::move(ref_id),
      },
  };
}

void test_service_metrics_bridge_emits_frozen_metric_families_and_samples() {
  using dasall::infra::metrics::MetricType;
  using dasall::services::DataQueryResult;
  using dasall::services::ExecutionCommandResult;
  using dasall::services::ExecutionSubscriptionResult;
  using dasall::services::internal::ServiceMetricsBridge;
  using dasall::services::internal::ServiceMetricsBridgeOptions;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<RecordingMeter>();
  auto provider = std::make_shared<RecordingMetricsProvider>(meter);
  ServiceMetricsBridge bridge(provider,
                              ServiceMetricsBridgeOptions{
                                  .enabled = true,
                                  .profile_id = "edge_balanced",
                                  .metrics_granularity = "full",
                                  .meter_scope_name = "services",
                                  .meter_scope_version = "v1",
                                  .now_ms = []() { return 1712736000000LL; },
                              });

  const auto command_result = bridge.record_execution_result(
      "safe_mode.enter",
      "service-primary",
      ExecutionCommandResult{
          .code = dasall::contracts::ResultCode::ProviderTimeout,
          .execution_id = "exec-025",
          .payload_json = "{}",
          .side_effects = {"safe_mode.enabled"},
          .compensation_hints = {"safe_mode.exit", "idempotency:required"},
          .error = make_error_info(dasall::contracts::ResultCodeCategory::Provider,
                                   "partial side effect",
                                   "execution_command_lane",
                                   "audit://partial-025"),
      },
      11U);
  const auto data_result = bridge.record_data_query_result(
      "status",
      DataQueryResult{
          .code = dasall::contracts::ResultCode::ToolExecutionFailed,
          .rows_json = "[{\"id\":1}]",
          .from_cache = true,
          .error = std::nullopt,
      },
      std::nullopt);
  const auto subscription_result = bridge.record_subscription_result(
      "cap.exec",
      "status",
      ExecutionSubscriptionResult{
          .code = dasall::contracts::ResultCode::RuntimeRetryExhausted,
          .events_json = "[{\"seq\":2}]",
          .next_cursor = std::string("2"),
          .resync_required = true,
          .dropped_count = 1U,
          .error = make_error_info(dasall::contracts::ResultCodeCategory::Runtime,
                                   "subscription overflow requires resync",
                                   "execution_subscription_hub",
                                   "subscription://cap.exec/status",
                                   true),
      });
  const auto circuit_result = bridge.record_execution_circuit_open(
      "safe_mode.enter",
      "service-primary",
      "route_unavailable");

  assert_true(command_result.emitted && command_result.has_consistent_state() &&
                  data_result.emitted && data_result.has_consistent_state() &&
                  subscription_result.emitted &&
                  subscription_result.has_consistent_state() &&
                  circuit_result.emitted && circuit_result.has_consistent_state(),
              "service metrics bridge should emit accepted command/data/subscription/circuit samples through the metrics provider");
  assert_true(!bridge.is_degraded(),
              "successful metric emissions should keep the service metrics bridge healthy");
  assert_true(bridge.has_active_meter() && bridge.instruments_registered(),
              "first successful emission should acquire the services meter scope and register the frozen instruments");
  assert_equal(std::string("services"),
               provider->last_scope.name,
               "service metrics bridge should request the frozen services meter scope");
  assert_equal(std::string("v1"),
               provider->last_scope.version,
               "service metrics bridge should preserve the frozen meter scope version");
  assert_equal(6,
               static_cast<int>(meter->created_identities.size()),
               "service metrics bridge should register the six frozen services metric families on first use");
  assert_true(std::any_of(meter->created_identities.begin(),
                          meter->created_identities.end(),
                          [](const auto& identity) {
                            return identity.name == "services_execution_requests_total" &&
                                   identity.type == MetricType::Counter && identity.unit == "1";
                          }) &&
                  std::any_of(meter->created_identities.begin(),
                              meter->created_identities.end(),
                              [](const auto& identity) {
                                return identity.name == "services_execution_latency_ms" &&
                                       identity.type == MetricType::Histogram && identity.unit == "ms";
                              }) &&
                  std::any_of(meter->created_identities.begin(),
                              meter->created_identities.end(),
                              [](const auto& identity) {
                                return identity.name == "services_subscription_overflow_total" &&
                                       identity.type == MetricType::Counter && identity.unit == "1";
                              }),
              "service metrics bridge should preserve the frozen name/type/unit contract for request, latency, and overflow metric families");
  assert_equal(6,
               static_cast<int>(meter->recorded_samples.size()),
               "command, latency, compensation, data cache hit, overflow, and circuit-open should emit six metric samples in total");

  const auto command_sample = find_sample(meter->recorded_samples,
                                          "services_execution_requests_total",
                                          "execution.command.safe_mode.enter");
  const auto hint_sample = find_sample(meter->recorded_samples,
                                       "services_compensation_hint_total",
                                       "execution.compensation.safe_mode.enter");
  const auto cache_hit_sample = find_sample(meter->recorded_samples,
                                            "services_data_query_requests_total",
                                            "data.query.status.cache_hit");
  const auto overflow_sample = find_sample(meter->recorded_samples,
                                           "services_subscription_overflow_total",
                                           "subscription.status.cap.exec");

  assert_true(command_sample.has_value() && command_sample->labels.module == "services" &&
                  command_sample->labels.profile == "edge_balanced" &&
                  command_sample->labels.outcome == "degraded" &&
                  command_sample->labels.error_code == "ProviderTimeout",
              "service metrics bridge should project partial-side-effect command results into the frozen module/profile/outcome/error label tuple");
  assert_true(hint_sample.has_value() && hint_sample->value == 2.0,
              "service metrics bridge should export the compensation hint count as a counter sample value");
  assert_true(cache_hit_sample.has_value() && cache_hit_sample->labels.outcome == "success",
              "service metrics bridge should mark cache hits on the frozen data.query.*.cache_hit stage while preserving successful outcome");
  assert_true(overflow_sample.has_value() && overflow_sample->labels.outcome == "degraded",
              "service metrics bridge should export subscription overflow as a degraded overflow sample");
}

void test_service_metrics_bridge_degrades_when_meter_record_fails() {
  using dasall::contracts::ResultCode;
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::infra::metrics::MetricsOperationStatus;
  using dasall::services::ExecutionCommandResult;
  using dasall::services::internal::ServiceMetricsBridge;
  using dasall::services::internal::ServiceMetricsBridgeOptions;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<RecordingMeter>();
  meter->scripted_record_results.push_back(MetricsOperationStatus::failure(
      ResultCode::ProviderTimeout,
      "metrics exporter timed out",
      "metrics.record",
      "RecordingMeter"));

  auto provider = std::make_shared<RecordingMetricsProvider>(meter);
  ServiceMetricsBridge bridge(provider,
                              ServiceMetricsBridgeOptions{
                                  .enabled = true,
                                  .profile_id = "desktop_full",
                                  .metrics_granularity = "full",
                                  .meter_scope_name = "services",
                                  .meter_scope_version = "v1",
                                  .now_ms = []() { return 1712736001000LL; },
                              });

  const auto result = bridge.record_execution_result(
      "toggle",
      "service-primary",
      ExecutionCommandResult{
          .code = ResultCode::ToolExecutionFailed,
          .execution_id = "exec-025-fail",
          .payload_json = "{}",
          .side_effects = {},
          .compensation_hints = {},
          .error = std::nullopt,
      },
      6U);

  assert_true(!result.emitted && result.has_consistent_state(),
              "meter record failures should return a failed service metrics emit result instead of pretending the sample was exported");
  assert_true(result.bridge_degraded,
              "provider/exporter failures should mark the service metrics bridge as degraded");
  assert_true(result.metrics_error_code == MetricsErrorCode::ExportFailure,
              "provider timeout should normalize to MET_E_EXPORT_FAILURE for service bridge diagnostics");
  assert_true(bridge.is_degraded(),
              "service metrics bridge should enter degraded mode after exporter failure");
  assert_equal(1,
               static_cast<int>(bridge.emission_failure_total()),
               "failed record should increase the bridge failure counter");
}

void test_service_metrics_bridge_treats_disabled_observability_as_noop() {
  using dasall::services::ExecutionCommandResult;
  using dasall::services::internal::ServiceMetricsBridge;
  using dasall::services::internal::ServiceMetricsBridgeOptions;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ServiceMetricsBridge bridge(nullptr,
                              ServiceMetricsBridgeOptions{
                                  .enabled = false,
                                  .profile_id = "edge_minimal",
                                  .metrics_granularity = "minimal",
                                  .meter_scope_name = "services",
                                  .meter_scope_version = "v1",
                                  .now_ms = []() { return 1712736002000LL; },
                              });

  const auto result = bridge.record_execution_result(
      "toggle",
      "service-primary",
      ExecutionCommandResult{
          .code = dasall::contracts::ResultCode::ToolExecutionFailed,
          .execution_id = "exec-025-disabled",
          .payload_json = "{}",
          .side_effects = {},
          .compensation_hints = {},
          .error = std::nullopt,
      },
      5U);

  assert_true(!result.emitted && result.signal_suppressed && result.status.ok,
              "disabled observability should turn service metric writes into successful no-op emissions");
  assert_true(!bridge.is_degraded(),
              "disabled observability should not degrade the metrics bridge");
  assert_equal(0,
               static_cast<int>(bridge.emission_attempt_total()),
               "disabled observability should avoid touching the metrics provider or meter registry");
}

}  // namespace

int main() {
  try {
    test_service_metrics_bridge_emits_frozen_metric_families_and_samples();
    test_service_metrics_bridge_degrades_when_meter_record_fails();
    test_service_metrics_bridge_treats_disabled_observability_as_noop();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}