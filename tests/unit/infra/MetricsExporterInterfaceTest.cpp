#include <exception>
#include <iostream>
#include <type_traits>

#include "metrics/IMetricExporter.h"
#include "support/TestAssertions.h"

namespace {

class NullMetricExporter final : public dasall::infra::metrics::IMetricExporter {
 public:
  dasall::infra::metrics::MetricsOperationStatus export_batch(
      const dasall::infra::metrics::MetricExportBatch& batch) override {
    if (!batch.is_valid()) {
      return dasall::infra::metrics::MetricsOperationStatus::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "metric export batch must keep batch_id, sample_count and exporter_type explicit",
          "metrics.export_batch",
          "NullMetricExporter");
    }

    return dasall::infra::metrics::MetricsOperationStatus::success(batch.batch_id);
  }

  dasall::infra::metrics::MetricsOperationStatus force_flush(
      const dasall::infra::metrics::MetricsCallDeadline& timeout) override {
    if (!timeout.is_valid()) {
      return dasall::infra::metrics::MetricsOperationStatus::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "metric exporter force_flush timeout must stay explicit",
          "metrics.force_flush",
          "NullMetricExporter");
    }

    return dasall::infra::metrics::MetricsOperationStatus::success("metrics-exporter://flush");
  }

  dasall::infra::metrics::MetricsOperationStatus shutdown(
      const dasall::infra::metrics::MetricsCallDeadline& timeout) override {
    if (!timeout.is_valid()) {
      return dasall::infra::metrics::MetricsOperationStatus::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "metric exporter shutdown timeout must stay explicit",
          "metrics.shutdown",
          "NullMetricExporter");
    }

    return dasall::infra::metrics::MetricsOperationStatus::success("metrics-exporter://shutdown");
  }
};

void test_metric_exporter_interface_accepts_valid_batch_and_lifecycle_inputs() {
  using dasall::infra::metrics::MetricExportBatch;
  using dasall::infra::metrics::MetricsCallDeadline;
  using dasall::infra::metrics::MetricsOperationStatus;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(MetricsOperationStatus{}.state_ref), std::string>);

  NullMetricExporter exporter;
  const auto export_result = exporter.export_batch(MetricExportBatch{
      .batch_id = std::string("metrics-batch-001"),
      .sample_count = 4,
      .exporter_type = std::string("noop"),
  });
  assert_true(export_result.ok && export_result.state_ref == "metrics-batch-001",
              "IMetricExporter skeleton should accept a valid batch placeholder and echo a stable batch reference");

  const auto flush_result = exporter.force_flush(MetricsCallDeadline{.timeout_ms = 200});
  assert_true(flush_result.ok,
              "IMetricExporter skeleton should accept a positive force_flush timeout placeholder");

  const auto shutdown_result = exporter.shutdown(MetricsCallDeadline{.timeout_ms = 200});
  assert_true(shutdown_result.ok,
              "IMetricExporter skeleton should accept a positive shutdown timeout placeholder");
}

void test_metric_exporter_interface_reports_invalid_inputs_observably() {
  using dasall::infra::metrics::MetricExportBatch;
  using dasall::infra::metrics::MetricsCallDeadline;
  using dasall::tests::support::assert_true;

  NullMetricExporter exporter;
  const auto invalid_export = exporter.export_batch(MetricExportBatch{});
  assert_true(!invalid_export.ok,
              "IMetricExporter skeleton should reject empty export batch placeholders");
  assert_true(invalid_export.references_only_contract_error_types(),
              "export failures should stay inside contracts ResultCode/ErrorInfo types");

  const auto invalid_flush = exporter.force_flush(MetricsCallDeadline{});
  assert_true(!invalid_flush.ok,
              "IMetricExporter skeleton should reject an unset force_flush timeout placeholder");
  assert_true(invalid_flush.references_only_contract_error_types(),
              "force_flush failures should stay inside contracts ResultCode/ErrorInfo types");

  const auto invalid_shutdown = exporter.shutdown(MetricsCallDeadline{});
  assert_true(!invalid_shutdown.ok,
              "IMetricExporter skeleton should reject an unset shutdown timeout placeholder");
  assert_true(invalid_shutdown.references_only_contract_error_types(),
              "shutdown failures should stay inside contracts ResultCode/ErrorInfo types");
}

}  // namespace

int main() {
  try {
    test_metric_exporter_interface_accepts_valid_batch_and_lifecycle_inputs();
    test_metric_exporter_interface_reports_invalid_inputs_observably();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}