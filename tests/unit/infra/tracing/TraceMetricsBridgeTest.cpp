#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "tracing/TraceMetricsBridge.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

class RecordingMeter final : public dasall::infra::metrics::IMeter {
 public:
  std::optional<dasall::infra::metrics::InstrumentHandle> create_counter(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities_.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":counter",
    };
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_gauge(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities_.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":gauge",
    };
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_histogram(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities_.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":histogram",
    };
  }

  dasall::infra::metrics::MetricsOperationStatus record(
      const dasall::infra::metrics::MetricSample& sample) override {
    recorded_samples_.push_back(sample);
    if (scripted_record_results_.empty()) {
      return dasall::infra::metrics::MetricsOperationStatus::success(
          "metrics://trace-recorded");
    }

    auto result = scripted_record_results_.front();
    scripted_record_results_.pop_front();
    return result;
  }

  void push_record_result(dasall::infra::metrics::MetricsOperationStatus result) {
    scripted_record_results_.push_back(std::move(result));
  }

  [[nodiscard]] const std::vector<dasall::infra::metrics::MetricIdentity>&
  created_identities() const {
    return created_identities_;
  }

  [[nodiscard]] const std::vector<dasall::infra::metrics::MetricSample>&
  recorded_samples() const {
    return recorded_samples_;
  }

 private:
  std::vector<dasall::infra::metrics::MetricIdentity> created_identities_;
  std::vector<dasall::infra::metrics::MetricSample> recorded_samples_;
  std::deque<dasall::infra::metrics::MetricsOperationStatus>
      scripted_record_results_;
};

class RecordingMetricsProvider final
    : public dasall::infra::metrics::IMetricsProvider {
 public:
  explicit RecordingMetricsProvider(std::shared_ptr<RecordingMeter> meter)
      : meter_(std::move(meter)) {}

  dasall::infra::metrics::MetricsOperationStatus init(
      const dasall::infra::metrics::MetricsProviderConfig&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://trace-provider-init");
  }

  std::shared_ptr<dasall::infra::metrics::IMeter> get_meter(
      const dasall::infra::metrics::MeterScope& scope) override {
    last_scope_ = scope;
    ++get_meter_call_total_;
    return meter_;
  }

  dasall::infra::metrics::MetricsOperationStatus force_flush(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://trace-provider-flush");
  }

  dasall::infra::metrics::MetricsOperationStatus shutdown(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://trace-provider-shutdown");
  }

  [[nodiscard]] const dasall::infra::metrics::MeterScope& last_scope() const {
    return last_scope_;
  }

  [[nodiscard]] std::uint64_t get_meter_call_total() const {
    return get_meter_call_total_;
  }

 private:
  std::shared_ptr<RecordingMeter> meter_;
  dasall::infra::metrics::MeterScope last_scope_{};
  std::uint64_t get_meter_call_total_ = 0;
};

void test_trace_metrics_bridge_emits_counter_gauge_and_histogram_samples() {
  using dasall::infra::tracing::TraceMetricKind;
  using dasall::infra::tracing::TraceMetricSignal;
  using dasall::infra::tracing::TraceMetricsBridge;
  using dasall::infra::tracing::TraceErrorCode;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<RecordingMeter>();
  auto provider = std::make_shared<RecordingMetricsProvider>(meter);
  TraceMetricsBridge bridge(provider, "desktop_full");

  const auto export_failure = bridge.emit(TraceMetricSignal{
      .kind = TraceMetricKind::ExportFailureTotal,
      .value = 1.0,
      .ts_unix_ms = 1712486400000,
      .stage = std::string("export"),
      .outcome = std::string("failure"),
      .trace_error_code = TraceErrorCode::ExportFailure,
  });
  const auto queue_depth = bridge.emit(TraceMetricSignal{
      .kind = TraceMetricKind::BatchQueueDepth,
      .value = 3.0,
      .ts_unix_ms = 1712486400100,
      .stage = std::string("queue"),
      .outcome = std::string("degraded"),
      .trace_error_code = std::nullopt,
  });
  const auto export_latency = bridge.emit(TraceMetricSignal{
      .kind = TraceMetricKind::ExportLatencyMs,
      .value = 27.0,
      .ts_unix_ms = 1712486400200,
      .stage = std::string("export"),
      .outcome = std::string("failure"),
      .trace_error_code = TraceErrorCode::ExportFailure,
  });

  assert_true(export_failure.emitted && export_failure.has_consistent_state() &&
                  queue_depth.emitted && queue_depth.has_consistent_state() &&
                  export_latency.emitted && export_latency.has_consistent_state(),
              "trace metrics bridge should emit accepted counter, gauge and histogram signals through the metrics provider");
  assert_true(export_failure.references_only_contract_error_types() &&
                  queue_depth.references_only_contract_error_types() &&
                  export_latency.references_only_contract_error_types(),
              "successful trace metrics bridge emissions should stay inside contracts ResultCode/ErrorInfo types");
  assert_true(!bridge.is_degraded(),
              "successful trace metric emissions should keep the bridge healthy");
  assert_true(bridge.has_active_meter() && bridge.instruments_registered(),
              "first successful trace metric emission should acquire the infra.tracing meter and register all frozen instruments");
  assert_equal(std::string("infra.tracing"),
               provider->last_scope().name,
               "trace metrics bridge should request the frozen infra.tracing meter scope");
  assert_equal(std::string("v1"),
               provider->last_scope().version,
               "trace metrics bridge should preserve the frozen tracing meter scope version");
  assert_equal(8,
               static_cast<int>(meter->created_identities().size()),
               "trace metrics bridge should register the eight frozen tracing metric families on first use");
  assert_equal(3,
               static_cast<int>(meter->recorded_samples().size()),
               "trace metrics bridge should emit one sample per accepted signal");
  assert_equal(std::string("tracing"),
               meter->recorded_samples().front().labels.module,
               "trace bridge samples should pin module=tracing");
  assert_equal(std::string("TRC_E_EXPORT_FAILURE"),
               meter->recorded_samples().front().labels.error_code,
               "trace export failure samples should keep the frozen tracing error token in MetricLabels.error_code");
  assert_equal(3.0,
               meter->recorded_samples()[1].value,
               "trace batch queue depth should be exported as the current gauge sample value");
  assert_equal(3,
               static_cast<int>(bridge.emission_attempt_total()),
               "three emit calls should increment the trace bridge attempt counter three times");
  assert_equal(0,
               static_cast<int>(bridge.emission_failure_total()),
               "successful trace metric emissions should not increase the bridge failure counter");
}

void test_trace_metrics_bridge_degrades_when_meter_record_fails() {
  using dasall::contracts::ResultCode;
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::infra::metrics::MetricsOperationStatus;
  using dasall::infra::tracing::TraceMetricKind;
  using dasall::infra::tracing::TraceMetricSignal;
  using dasall::infra::tracing::TraceMetricsBridge;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<RecordingMeter>();
  meter->push_record_result(MetricsOperationStatus::failure(
      ResultCode::ProviderTimeout,
      "metrics exporter timed out",
      "metrics.record",
      "RecordingMeter"));

  auto provider = std::make_shared<RecordingMetricsProvider>(meter);
  TraceMetricsBridge bridge(provider, "desktop_full");

  const auto result = bridge.emit(TraceMetricSignal{
      .kind = TraceMetricKind::ExportLatencyMs,
      .value = 91.0,
      .ts_unix_ms = 1712486401000,
      .stage = std::string("export"),
      .outcome = std::string("failure"),
      .trace_error_code = dasall::infra::tracing::TraceErrorCode::ExportTimeout,
  });

  assert_true(!result.emitted && result.has_consistent_state(),
              "metrics record failures should return a failed trace bridge result instead of pretending emission succeeded");
  assert_true(result.bridge_degraded,
              "provider/exporter failures should mark the trace metrics bridge degraded");
  assert_true(result.references_only_contract_error_types(),
              "trace metrics bridge failures should stay inside contracts ResultCode/ErrorInfo types");
  assert_true(result.metrics_error_code == MetricsErrorCode::ExportFailure,
              "provider timeout should be normalized to MET_E_EXPORT_FAILURE for trace bridge diagnostics");
  assert_true(bridge.is_degraded(),
              "trace metrics bridge should enter degraded mode after metrics provider failure");
  assert_equal(1,
               static_cast<int>(bridge.emission_failure_total()),
               "failed trace metric emission should increase the bridge failure counter");
}

void test_trace_metrics_bridge_rejects_invalid_stage_before_recording() {
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::infra::tracing::TraceErrorCode;
  using dasall::infra::tracing::TraceMetricKind;
  using dasall::infra::tracing::TraceMetricSignal;
  using dasall::infra::tracing::TraceMetricsBridge;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<RecordingMeter>();
  auto provider = std::make_shared<RecordingMetricsProvider>(meter);
  TraceMetricsBridge bridge(provider, "desktop_full");

  const auto result = bridge.emit(TraceMetricSignal{
      .kind = TraceMetricKind::ContextInvalidTotal,
      .value = 1.0,
      .ts_unix_ms = 1712486402000,
      .stage = std::string("trace-request-123"),
      .outcome = std::string("failure"),
      .trace_error_code = TraceErrorCode::InvalidContext,
  });

  assert_true(!result.emitted && result.has_consistent_state(),
              "non-whitelist trace metric stages should be rejected before the bridge touches the metrics provider");
  assert_true(result.bridge_degraded,
              "bridge-local label contract violations should move the trace metrics bridge into degraded mode");
  assert_true(result.metrics_error_code == MetricsErrorCode::ConfigInvalid,
              "invalid trace metric stages should be normalized to MET_E_CONFIG_INVALID");
  assert_equal(0,
               static_cast<int>(provider->get_meter_call_total()),
               "invalid trace metric labels should not fetch a meter from the provider");
  assert_equal(0,
               static_cast<int>(meter->recorded_samples().size()),
               "invalid trace metric labels should not attempt to emit a sample");
}

}  // namespace

int main() {
  try {
    test_trace_metrics_bridge_emits_counter_gauge_and_histogram_samples();
    test_trace_metrics_bridge_degrades_when_meter_record_fails();
    test_trace_metrics_bridge_rejects_invalid_stage_before_recording();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}