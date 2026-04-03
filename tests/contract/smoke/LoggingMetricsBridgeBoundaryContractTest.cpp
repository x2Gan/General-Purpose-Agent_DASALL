#include <algorithm>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "logging/LoggingMetricsBridge.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

class BoundaryMeter final : public dasall::infra::metrics::IMeter {
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
    if (scripted_results_.empty()) {
      return dasall::infra::metrics::MetricsOperationStatus::success(
          "metrics://boundary-record");
    }

    auto result = scripted_results_.front();
    scripted_results_.pop_front();
    return result;
  }

  void push_record_result(dasall::infra::metrics::MetricsOperationStatus result) {
    scripted_results_.push_back(std::move(result));
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
  std::deque<dasall::infra::metrics::MetricsOperationStatus> scripted_results_;
};

class BoundaryProvider final : public dasall::infra::metrics::IMetricsProvider {
 public:
  explicit BoundaryProvider(std::shared_ptr<BoundaryMeter> meter)
      : meter_(std::move(meter)) {}

  dasall::infra::metrics::MetricsOperationStatus init(
      const dasall::infra::metrics::MetricsProviderConfig&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://boundary-provider-init");
  }

  std::shared_ptr<dasall::infra::metrics::IMeter> get_meter(
      const dasall::infra::metrics::MeterScope& scope) override {
    last_scope_ = scope;
    return meter_;
  }

  dasall::infra::metrics::MetricsOperationStatus force_flush(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://boundary-provider-flush");
  }

  dasall::infra::metrics::MetricsOperationStatus shutdown(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://boundary-provider-shutdown");
  }

  [[nodiscard]] const dasall::infra::metrics::MeterScope& last_scope() const {
    return last_scope_;
  }

 private:
  std::shared_ptr<BoundaryMeter> meter_;
  dasall::infra::metrics::MeterScope last_scope_{};
};

bool is_frozen_logging_metric_name(std::string_view name) {
  return name == dasall::infra::logging::logging_metric_name(
                     dasall::infra::logging::LoggingMetricKind::WriteTotal) ||
         name == dasall::infra::logging::logging_metric_name(
                     dasall::infra::logging::LoggingMetricKind::WriteFailTotal) ||
         name == dasall::infra::logging::logging_metric_name(
                     dasall::infra::logging::LoggingMetricKind::DropTotal) ||
         name == dasall::infra::logging::logging_metric_name(
                     dasall::infra::logging::LoggingMetricKind::QueueDepth) ||
         name == dasall::infra::logging::logging_metric_name(
                     dasall::infra::logging::LoggingMetricKind::FlushLatencyMs);
}

void test_logging_metrics_bridge_boundary_keeps_contract_types_and_frozen_scope() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::logging::LoggingMetricKind;
  using dasall::infra::logging::LoggingMetricSignal;
  using dasall::infra::logging::LoggingMetricsBridge;
  using dasall::infra::logging::LoggingMetricsEmitResult;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(LoggingMetricsEmitResult{}.status.result_code),
                               ResultCode>);
  static_assert(std::is_same_v<decltype(LoggingMetricsEmitResult{}.status.error),
                               std::optional<ErrorInfo>>);
  static_assert(std::is_same_v<decltype(&LoggingMetricsBridge::emit),
                               LoggingMetricsEmitResult (LoggingMetricsBridge::*)(
                                   const dasall::infra::logging::LoggingMetricSignal&)>);

  auto meter = std::make_shared<BoundaryMeter>();
  auto provider = std::make_shared<BoundaryProvider>(meter);
  LoggingMetricsBridge bridge(provider, "edge_balanced");

  const auto result = bridge.emit(LoggingMetricSignal{
      .kind = LoggingMetricKind::FlushLatencyMs,
      .value = 12.0,
      .ts_unix_ms = 1712140803000,
      .stage = std::string("flush"),
      .outcome = std::string("degraded"),
      .logging_error_code = dasall::infra::logging::LoggingErrorCode::SinkIo,
  });

  assert_true(result.emitted,
              "logging metrics bridge boundary should accept frozen flush latency observations");
  assert_equal(std::string("infra.logging"),
               provider->last_scope().name,
               "boundary bridge should always request the frozen infra.logging meter scope");
  assert_equal(std::string("v1"),
               provider->last_scope().version,
               "boundary bridge should always preserve the frozen meter scope version");
  for (const auto& identity : meter->created_identities()) {
    assert_true(is_frozen_logging_metric_name(identity.name),
                "boundary bridge should only register the five frozen logging metric families");
    assert_true(identity.is_valid(),
                "boundary bridge should only register valid MetricIdentity objects");
  }
  assert_true(!meter->recorded_samples().empty(),
              "boundary bridge should emit at least one sample when a frozen signal is accepted");
  const auto& labels = meter->recorded_samples().back().labels;
  assert_true(labels.module == "logging",
              "boundary bridge should pin module=logging in MetricLabels");
  assert_true(dasall::infra::logging::is_logging_metric_stage(labels.stage),
              "boundary bridge should keep stage inside the frozen allowlist");
  assert_true(dasall::infra::logging::is_logging_metric_outcome(labels.outcome),
              "boundary bridge should keep outcome inside the frozen allowlist");
  assert_true(dasall::infra::logging::is_logging_metric_error_code(labels.error_code),
              "boundary bridge should keep error_code inside the frozen allowlist");
}

void test_logging_metrics_bridge_boundary_rejects_non_whitelist_stage() {
  using dasall::infra::logging::LoggingMetricKind;
  using dasall::infra::logging::LoggingMetricSignal;
  using dasall::infra::logging::LoggingMetricsBridge;
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<BoundaryMeter>();
  auto provider = std::make_shared<BoundaryProvider>(meter);
  LoggingMetricsBridge bridge(provider);

  const auto result = bridge.emit(LoggingMetricSignal{
      .kind = LoggingMetricKind::QueueDepth,
      .value = 7.0,
      .ts_unix_ms = 1712140804000,
      .stage = std::string("request-7c8d56"),
      .outcome = std::string("success"),
      .logging_error_code = std::nullopt,
  });

  assert_true(!result.emitted,
              "boundary bridge should reject non-whitelist stage labels before sample emission");
  assert_true(result.metrics_error_code == MetricsErrorCode::ConfigInvalid,
              "boundary bridge should normalize non-whitelist labels to MET_E_CONFIG_INVALID");
  assert_true(meter->recorded_samples().empty(),
              "boundary bridge should not emit any sample once the label contract is violated");
}

}  // namespace

int main() {
  try {
    test_logging_metrics_bridge_boundary_keeps_contract_types_and_frozen_scope();
    test_logging_metrics_bridge_boundary_rejects_non_whitelist_stage();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}