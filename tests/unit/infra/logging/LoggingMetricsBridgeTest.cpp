#include <algorithm>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "logging/LoggingMetricsBridge.h"
#include "support/TestAssertions.h"

namespace {

class RecordingMeter final : public dasall::infra::metrics::IMeter {
 public:
  std::optional<dasall::infra::metrics::InstrumentHandle> create_counter(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities_.push_back(identity);
    if (fail_registration_) {
      return std::nullopt;
    }

    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":counter",
    };
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_gauge(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities_.push_back(identity);
    if (fail_registration_) {
      return std::nullopt;
    }

    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":gauge",
    };
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_histogram(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities_.push_back(identity);
    if (fail_registration_) {
      return std::nullopt;
    }

    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":histogram",
    };
  }

  dasall::infra::metrics::MetricsOperationStatus record(
      const dasall::infra::metrics::MetricSample& sample) override {
    recorded_samples_.push_back(sample);
    if (scripted_record_results_.empty()) {
      return dasall::infra::metrics::MetricsOperationStatus::success(
          "metrics://recorded");
    }

    auto result = scripted_record_results_.front();
    scripted_record_results_.pop_front();
    return result;
  }

  void push_record_result(dasall::infra::metrics::MetricsOperationStatus result) {
    scripted_record_results_.push_back(std::move(result));
  }

  void set_fail_registration(bool fail_registration) {
    fail_registration_ = fail_registration;
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
  bool fail_registration_ = false;
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
        "metrics://provider-init");
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
        "metrics://provider-flush");
  }

  dasall::infra::metrics::MetricsOperationStatus shutdown(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://provider-shutdown");
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

void test_logging_metrics_bridge_emits_samples_through_provider_and_meter() {
  using dasall::infra::logging::LoggingMetricKind;
  using dasall::infra::logging::LoggingMetricSignal;
  using dasall::infra::logging::LoggingMetricsBridge;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<RecordingMeter>();
  auto provider = std::make_shared<RecordingMetricsProvider>(meter);
  LoggingMetricsBridge bridge(provider, "desktop_full");

  const auto result = bridge.emit(LoggingMetricSignal{
      .kind = LoggingMetricKind::WriteTotal,
      .value = 1.0,
      .ts_unix_ms = 1712140800000,
      .stage = std::string("write"),
      .outcome = std::string("success"),
      .logging_error_code = std::nullopt,
  });

  assert_true(result.emitted && result.has_consistent_state(),
              "logging metrics bridge should emit a valid write_total sample through provider and meter");
  assert_true(result.references_only_contract_error_types(),
              "successful bridge emissions should remain representable inside contracts ResultCode/ErrorInfo types");
  assert_true(!bridge.is_degraded(),
              "successful bridge emissions should keep the bridge out of degraded mode");
  assert_true(bridge.has_active_meter() && bridge.instruments_registered(),
              "first successful emit should acquire the infra.logging meter and register the frozen instrument set");
  assert_equal(std::string("infra.logging"),
               provider->last_scope().name,
               "logging metrics bridge should request the frozen infra.logging meter scope");
  assert_equal(std::string("v1"),
               provider->last_scope().version,
               "logging metrics bridge should keep the frozen meter scope version");
  assert_equal(5,
               static_cast<int>(meter->created_identities().size()),
               "logging metrics bridge should register the five frozen metric families on first use");
  assert_equal(1,
               static_cast<int>(meter->recorded_samples().size()),
               "logging metrics bridge should emit exactly one sample for one signal");
  assert_equal(std::string("logging"),
               meter->recorded_samples().front().labels.module,
               "logging bridge samples should keep module=logging");
  assert_equal(std::string("desktop_full"),
               meter->recorded_samples().front().labels.profile,
               "logging bridge samples should preserve the active profile id");
  assert_equal(std::string("none"),
               meter->recorded_samples().front().labels.error_code,
               "successful logging bridge samples should use the frozen none error_code label");
  assert_equal(1,
               static_cast<int>(bridge.emission_attempt_total()),
               "one emit call should increment the attempt counter once");
  assert_equal(0,
               static_cast<int>(bridge.emission_failure_total()),
               "successful emit should not increase the bridge failure counter");
}

void test_logging_metrics_bridge_degrades_when_meter_record_fails() {
  using dasall::contracts::ResultCode;
  using dasall::infra::logging::LoggingErrorCode;
  using dasall::infra::logging::LoggingMetricKind;
  using dasall::infra::logging::LoggingMetricSignal;
  using dasall::infra::logging::LoggingMetricsBridge;
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::infra::metrics::MetricsOperationStatus;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<RecordingMeter>();
  meter->push_record_result(MetricsOperationStatus::failure(
      ResultCode::ProviderTimeout,
      "metrics exporter timed out",
      "metrics.record",
      "RecordingMeter"));

  auto provider = std::make_shared<RecordingMetricsProvider>(meter);
  LoggingMetricsBridge bridge(provider, "desktop_full");

  const auto result = bridge.emit(LoggingMetricSignal{
      .kind = LoggingMetricKind::WriteFailTotal,
      .value = 1.0,
      .ts_unix_ms = 1712140801000,
      .stage = std::string("recovery"),
      .outcome = std::string("failure"),
      .logging_error_code = LoggingErrorCode::SinkIo,
  });

  assert_true(!result.emitted && result.has_consistent_state(),
              "meter record failures should return a failed emit result instead of pretending the sample was emitted");
  assert_true(result.bridge_degraded,
              "provider/exporter failures should mark the logging metrics bridge as degraded");
  assert_true(result.references_only_contract_error_types(),
              "bridge failures should stay inside contracts ResultCode/ErrorInfo types");
  assert_true(result.metrics_error_code == MetricsErrorCode::ExportFailure,
              "provider timeout should be normalized to MET_E_EXPORT_FAILURE for bridge diagnostics");
  assert_true(bridge.is_degraded(),
              "provider timeout should move the logging metrics bridge into degraded mode");
  assert_equal(1,
               static_cast<int>(bridge.emission_failure_total()),
               "failed record should increase the bridge failure counter");
  assert_equal(1,
               static_cast<int>(meter->recorded_samples().size()),
               "even failed record attempts should still expose the sample shape to the fake meter for diagnostics");
}

void test_logging_metrics_bridge_rejects_invalid_stage_before_recording() {
  using dasall::infra::logging::LoggingMetricKind;
  using dasall::infra::logging::LoggingMetricSignal;
  using dasall::infra::logging::LoggingMetricsBridge;
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<RecordingMeter>();
  auto provider = std::make_shared<RecordingMetricsProvider>(meter);
  LoggingMetricsBridge bridge(provider, "desktop_full");

  const auto result = bridge.emit(LoggingMetricSignal{
      .kind = LoggingMetricKind::QueueDepth,
      .value = 42.0,
      .ts_unix_ms = 1712140802000,
      .stage = std::string("trace-request-123"),
      .outcome = std::string("success"),
      .logging_error_code = std::nullopt,
  });

  assert_true(!result.emitted && result.has_consistent_state(),
              "invalid stage labels should be rejected before the bridge touches the metrics provider");
  assert_true(result.bridge_degraded,
              "invalid bridge-local config should put the bridge in degraded mode until a valid signal is provided");
  assert_true(result.metrics_error_code == MetricsErrorCode::ConfigInvalid,
              "invalid stage labels should be normalized to MET_E_CONFIG_INVALID");
  assert_equal(0,
               static_cast<int>(provider->get_meter_call_total()),
               "invalid bridge-local labels should not fetch a meter from the provider");
  assert_equal(0,
               static_cast<int>(meter->recorded_samples().size()),
               "invalid bridge-local labels should not attempt to emit a sample");
}

}  // namespace

int main() {
  try {
    test_logging_metrics_bridge_emits_samples_through_provider_and_meter();
    test_logging_metrics_bridge_degrades_when_meter_record_fails();
    test_logging_metrics_bridge_rejects_invalid_stage_before_recording();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}