#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "policy/PolicyMetricsBridge.h"
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

void test_policy_metrics_bridge_emits_counter_and_gauge_samples() {
  using dasall::infra::policy::PolicyMetricKind;
  using dasall::infra::policy::PolicyMetricSignal;
  using dasall::infra::policy::PolicyMetricsBridge;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<RecordingMeter>();
  auto provider = std::make_shared<RecordingMetricsProvider>(meter);
  PolicyMetricsBridge bridge(provider, "desktop_full");

  const auto reload_result = bridge.emit(PolicyMetricSignal{
      .kind = PolicyMetricKind::ReloadTotal,
      .value = 1.0,
      .ts_unix_ms = 1712140800000,
      .stage = std::string("load"),
      .outcome = std::string("success"),
      .policy_error_code = std::nullopt,
  });
  const auto generation_result = bridge.emit(PolicyMetricSignal{
      .kind = PolicyMetricKind::ActiveGeneration,
      .value = 7.0,
      .ts_unix_ms = 1712140800500,
      .stage = std::string("load"),
      .outcome = std::string("success"),
      .policy_error_code = std::nullopt,
  });

  assert_true(reload_result.emitted && reload_result.has_consistent_state() &&
                  generation_result.emitted &&
                  generation_result.has_consistent_state(),
              "policy metrics bridge should emit both counter and gauge samples through provider and meter");
  assert_true(reload_result.references_only_contract_error_types() &&
                  generation_result.references_only_contract_error_types(),
              "successful bridge emissions should remain representable inside contracts ResultCode/ErrorInfo types");
  assert_true(!bridge.is_degraded(),
              "successful bridge emissions should keep the bridge out of degraded mode");
  assert_true(bridge.has_active_meter() && bridge.instruments_registered(),
              "first successful emit should acquire the infra.policy meter and register the frozen instrument set");
  assert_equal(std::string("infra.policy"),
               provider->last_scope().name,
               "policy metrics bridge should request the frozen infra.policy meter scope");
  assert_equal(std::string("v1"),
               provider->last_scope().version,
               "policy metrics bridge should keep the frozen meter scope version");
  assert_equal(7,
               static_cast<int>(meter->created_identities().size()),
               "policy metrics bridge should register the seven frozen metric families on first use");
  assert_equal(2,
               static_cast<int>(meter->recorded_samples().size()),
               "policy metrics bridge should emit one sample per accepted signal");
  assert_equal(std::string("policy"),
               meter->recorded_samples().front().labels.module,
               "policy bridge samples should keep module=policy");
  assert_equal(std::string("none"),
               meter->recorded_samples().front().labels.error_code,
               "successful policy bridge samples should use the frozen none error_code label");
  assert_equal(7.0,
               meter->recorded_samples().back().value,
               "policy active_generation should be exported as a gauge sample value");
  assert_equal(2,
               static_cast<int>(bridge.emission_attempt_total()),
               "two emit calls should increment the attempt counter twice");
  assert_equal(0,
               static_cast<int>(bridge.emission_failure_total()),
               "successful emits should not increase the bridge failure counter");
}

void test_policy_metrics_bridge_degrades_when_meter_record_fails() {
  using dasall::contracts::ResultCode;
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::infra::metrics::MetricsOperationStatus;
  using dasall::infra::policy::PolicyErrorCode;
  using dasall::infra::policy::PolicyMetricKind;
  using dasall::infra::policy::PolicyMetricSignal;
  using dasall::infra::policy::PolicyMetricsBridge;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<RecordingMeter>();
  meter->push_record_result(MetricsOperationStatus::failure(
      ResultCode::ProviderTimeout,
      "metrics exporter timed out",
      "metrics.record",
      "RecordingMeter"));

  auto provider = std::make_shared<RecordingMetricsProvider>(meter);
  PolicyMetricsBridge bridge(provider, "desktop_full");

  const auto result = bridge.emit(PolicyMetricSignal{
      .kind = PolicyMetricKind::PatchTotal,
      .value = 1.0,
      .ts_unix_ms = 1712140801000,
      .stage = std::string("apply_patch"),
      .outcome = std::string("failure"),
      .policy_error_code = PolicyErrorCode::StoreCommitFailed,
  });

  assert_true(!result.emitted && result.has_consistent_state(),
              "meter record failures should return a failed emit result instead of pretending the sample was emitted");
  assert_true(result.bridge_degraded,
              "provider/exporter failures should mark the policy metrics bridge as degraded");
  assert_true(result.references_only_contract_error_types(),
              "bridge failures should stay inside contracts ResultCode/ErrorInfo types");
  assert_true(result.metrics_error_code == MetricsErrorCode::ExportFailure,
              "provider timeout should be normalized to MET_E_EXPORT_FAILURE for bridge diagnostics");
  assert_true(bridge.is_degraded(),
              "provider timeout should move the policy metrics bridge into degraded mode");
  assert_equal(1,
               static_cast<int>(bridge.emission_failure_total()),
               "failed record should increase the bridge failure counter");
  assert_equal(1,
               static_cast<int>(meter->recorded_samples().size()),
               "even failed record attempts should still expose the sample shape to the fake meter for diagnostics");
}

void test_policy_metrics_bridge_rejects_invalid_stage_before_recording() {
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::infra::policy::PolicyErrorCode;
  using dasall::infra::policy::PolicyMetricKind;
  using dasall::infra::policy::PolicyMetricSignal;
  using dasall::infra::policy::PolicyMetricsBridge;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<RecordingMeter>();
  auto provider = std::make_shared<RecordingMetricsProvider>(meter);
  PolicyMetricsBridge bridge(provider, "desktop_full");

  const auto result = bridge.emit(PolicyMetricSignal{
      .kind = PolicyMetricKind::DenyTotal,
      .value = 1.0,
      .ts_unix_ms = 1712140802000,
      .stage = std::string("trace-request-123"),
      .outcome = std::string("rejected"),
      .policy_error_code = PolicyErrorCode::QueryDenied,
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
    test_policy_metrics_bridge_emits_counter_and_gauge_samples();
    test_policy_metrics_bridge_degrades_when_meter_record_fails();
    test_policy_metrics_bridge_rejects_invalid_stage_before_recording();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}