#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "tracing/TraceMetricsBridge.h"
#include "support/TestAssertions.h"

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
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://trace-boundary-record");
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
};

class BoundaryProvider final : public dasall::infra::metrics::IMetricsProvider {
 public:
  explicit BoundaryProvider(std::shared_ptr<BoundaryMeter> meter)
      : meter_(std::move(meter)) {}

  dasall::infra::metrics::MetricsOperationStatus init(
      const dasall::infra::metrics::MetricsProviderConfig&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://trace-boundary-provider-init");
  }

  std::shared_ptr<dasall::infra::metrics::IMeter> get_meter(
      const dasall::infra::metrics::MeterScope& scope) override {
    last_scope_ = scope;
    return meter_;
  }

  dasall::infra::metrics::MetricsOperationStatus force_flush(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://trace-boundary-provider-flush");
  }

  dasall::infra::metrics::MetricsOperationStatus shutdown(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://trace-boundary-provider-shutdown");
  }

  [[nodiscard]] const dasall::infra::metrics::MeterScope& last_scope() const {
    return last_scope_;
  }

 private:
  std::shared_ptr<BoundaryMeter> meter_;
  dasall::infra::metrics::MeterScope last_scope_{};
};

bool is_frozen_trace_metric_name(const std::string_view& name) {
  return name == dasall::infra::tracing::trace_metric_name(
                     dasall::infra::tracing::TraceMetricKind::SpanStartedTotal) ||
         name == dasall::infra::tracing::trace_metric_name(
                     dasall::infra::tracing::TraceMetricKind::SpanEndedTotal) ||
         name == dasall::infra::tracing::trace_metric_name(
                     dasall::infra::tracing::TraceMetricKind::SpanDroppedTotal) ||
         name == dasall::infra::tracing::trace_metric_name(
                     dasall::infra::tracing::TraceMetricKind::ExportSuccessTotal) ||
         name == dasall::infra::tracing::trace_metric_name(
                     dasall::infra::tracing::TraceMetricKind::ExportFailureTotal) ||
         name == dasall::infra::tracing::trace_metric_name(
                     dasall::infra::tracing::TraceMetricKind::ExportLatencyMs) ||
         name == dasall::infra::tracing::trace_metric_name(
                     dasall::infra::tracing::TraceMetricKind::BatchQueueDepth) ||
         name == dasall::infra::tracing::trace_metric_name(
                     dasall::infra::tracing::TraceMetricKind::ContextInvalidTotal);
}

void test_trace_metrics_bridge_boundary_keeps_contract_types_and_frozen_scope() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::tracing::TraceMetricKind;
  using dasall::infra::tracing::TraceMetricSignal;
  using dasall::infra::tracing::TraceMetricsBridge;
  using dasall::infra::tracing::TraceMetricsEmitResult;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(TraceMetricsEmitResult{}.status.result_code),
                               ResultCode>);
  static_assert(std::is_same_v<decltype(TraceMetricsEmitResult{}.status.error),
                               std::optional<ErrorInfo>>);
  static_assert(std::is_same_v<decltype(&TraceMetricsBridge::emit),
                               TraceMetricsEmitResult (TraceMetricsBridge::*)(
                                   const dasall::infra::tracing::TraceMetricSignal&)>);

  auto meter = std::make_shared<BoundaryMeter>();
  auto provider = std::make_shared<BoundaryProvider>(meter);
  TraceMetricsBridge bridge(provider, "edge_balanced");

  const auto result = bridge.emit(TraceMetricSignal{
      .kind = TraceMetricKind::ExportFailureTotal,
      .value = 1.0,
      .ts_unix_ms = 1712486404000,
      .stage = std::string("export"),
      .outcome = std::string("failure"),
      .trace_error_code = dasall::infra::tracing::TraceErrorCode::ExportFailure,
  });

  assert_true(result.emitted,
              "trace metrics bridge boundary should accept frozen export failure observations");
  assert_equal(std::string("infra.tracing"),
               provider->last_scope().name,
               "boundary bridge should always request the frozen infra.tracing meter scope");
  assert_equal(std::string("v1"),
               provider->last_scope().version,
               "boundary bridge should always preserve the frozen tracing meter scope version");
  for (const auto& identity : meter->created_identities()) {
    assert_true(is_frozen_trace_metric_name(identity.name),
                "boundary bridge should only register the eight frozen tracing metric families");
    assert_true(identity.is_valid(),
                "boundary bridge should only register valid tracing MetricIdentity objects");
  }
  assert_true(!meter->recorded_samples().empty(),
              "boundary bridge should emit at least one sample when a frozen tracing signal is accepted");
  const auto& labels = meter->recorded_samples().back().labels;
  assert_true(labels.module == "tracing",
              "boundary bridge should pin module=tracing in MetricLabels");
  assert_true(dasall::infra::tracing::is_trace_metric_stage(labels.stage),
              "boundary bridge should keep stage inside the frozen tracing allowlist");
  assert_true(dasall::infra::tracing::is_trace_metric_outcome(labels.outcome),
              "boundary bridge should keep outcome inside the frozen tracing allowlist");
  assert_true(dasall::infra::tracing::is_trace_metric_error_code(labels.error_code),
              "boundary bridge should keep error_code inside the frozen tracing allowlist");
}

void test_trace_metrics_bridge_boundary_rejects_non_whitelist_stage() {
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::infra::tracing::TraceMetricKind;
  using dasall::infra::tracing::TraceMetricSignal;
  using dasall::infra::tracing::TraceMetricsBridge;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<BoundaryMeter>();
  auto provider = std::make_shared<BoundaryProvider>(meter);
  TraceMetricsBridge bridge(provider);

  const auto result = bridge.emit(TraceMetricSignal{
      .kind = TraceMetricKind::ContextInvalidTotal,
      .value = 1.0,
      .ts_unix_ms = 1712486404100,
      .stage = std::string("request-7c8d56"),
      .outcome = std::string("failure"),
      .trace_error_code = dasall::infra::tracing::TraceErrorCode::InvalidContext,
  });

  assert_true(!result.emitted,
              "boundary bridge should reject non-whitelist tracing stage labels before sample emission");
  assert_true(result.metrics_error_code == MetricsErrorCode::ConfigInvalid,
              "boundary bridge should normalize invalid tracing labels to MET_E_CONFIG_INVALID");
  assert_true(meter->recorded_samples().empty(),
              "boundary bridge should not emit any tracing sample once the label contract is violated");
}

}  // namespace

int main() {
  try {
    test_trace_metrics_bridge_boundary_keeps_contract_types_and_frozen_scope();
    test_trace_metrics_bridge_boundary_rejects_non_whitelist_stage();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}