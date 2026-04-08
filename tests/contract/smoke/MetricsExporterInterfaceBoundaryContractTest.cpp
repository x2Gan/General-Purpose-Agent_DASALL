#include <exception>
#include <iostream>
#include <optional>
#include <type_traits>

#include "../../../infra/include/metrics/IMetricExporter.h"
#include "support/TestAssertions.h"

namespace {

void test_metric_exporter_interface_keeps_export_surface_inside_local_batch_and_contract_errors() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::metrics::IMetricExporter;
  using dasall::infra::metrics::MetricExportBatch;
  using dasall::infra::metrics::MetricsOperationStatus;

  static_assert(std::is_same_v<decltype(&IMetricExporter::export_batch),
                               MetricsOperationStatus (IMetricExporter::*)(const MetricExportBatch&)>);
  static_assert(std::is_same_v<decltype(&IMetricExporter::force_flush),
                               MetricsOperationStatus (IMetricExporter::*)(
                                   const dasall::infra::metrics::MetricsCallDeadline&)>);
  static_assert(std::is_same_v<decltype(&IMetricExporter::shutdown),
                               MetricsOperationStatus (IMetricExporter::*)(
                                   const dasall::infra::metrics::MetricsCallDeadline&)>);
  static_assert(std::is_same_v<decltype(MetricsOperationStatus{}.result_code), ResultCode>);
  static_assert(std::is_same_v<decltype(MetricsOperationStatus{}.error), std::optional<ErrorInfo>>);
}

void test_metric_exporter_interface_keeps_batch_and_deadline_guards_binary() {
  using dasall::infra::metrics::MetricExportBatch;
  using dasall::infra::metrics::MetricsCallDeadline;
  using dasall::infra::metrics::MetricsOperationStatus;
  using dasall::tests::support::assert_true;

  const auto failure = MetricsOperationStatus::failure(
      dasall::contracts::ResultCode::ValidationFieldMissing,
      "metric export batch must stay explicit",
      "metrics.export_batch",
      "IMetricExporter");

  assert_true(MetricExportBatch{
                  .batch_id = std::string("metrics-batch-001"),
                  .sample_count = 1,
                  .exporter_type = std::string("noop"),
              }
                  .is_valid(),
              "metric export batch should remain valid when batch_id, sample_count and exporter_type are present");
  assert_true(!MetricExportBatch{}.is_valid(),
              "metric export batch should reject empty placeholders");
  assert_true(MetricsCallDeadline{.timeout_ms = 1}.is_valid(),
              "positive timeout values should satisfy the exporter deadline guard");
  assert_true(!MetricsCallDeadline{}.is_valid(),
              "zero timeout should remain invalid for exporter lifecycle operations");
  assert_true(!failure.ok,
              "metric exporter boundary failures should remain explicit failures");
  assert_true(failure.references_only_contract_error_types(),
              "IMetricExporter should expose only contracts ResultCode/ErrorInfo types");
}

}  // namespace

int main() {
  try {
    test_metric_exporter_interface_keeps_export_surface_inside_local_batch_and_contract_errors();
    test_metric_exporter_interface_keeps_batch_and_deadline_guards_binary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}