#include <exception>
#include <iostream>
#include <string>

#include "metrics/AggregationEngine.h"
#include "metrics/MetricReaderScheduler.h"
#include "metrics/MetricsConfigPolicy.h"
#include "metrics/MetricsErrors.h"
#include "metrics/MetricsExporterAdapter.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::infra::metrics::AggregationSnapshot make_snapshot() {
  using dasall::infra::metrics::AggregatedMetricValue;
  using dasall::infra::metrics::AggregationSnapshot;
  using dasall::infra::metrics::MetricIdentity;
  using dasall::infra::metrics::MetricType;

  AggregationSnapshot snapshot;
  snapshot.metrics.emplace(
      "metrics.jobs_total",
      AggregatedMetricValue{
          .identity = MetricIdentity{
              .name = std::string("metrics.jobs_total"),
              .type = MetricType::Counter,
              .unit = std::string("1"),
              .description = std::string("jobs processed"),
          },
          .sample_count = 3U,
          .accumulated_value = 3.0,
          .last_value = 3.0,
          .min_value = 1.0,
          .max_value = 2.0,
          .bucket_counts = {},
      });
  return snapshot;
}

void test_metrics_exporter_adapter_exports_prom_text_batches_from_reader_scheduler() {
  using dasall::infra::metrics::MetricReaderScheduler;
  using dasall::infra::metrics::MetricsConfigPatch;
  using dasall::infra::metrics::MetricsConfigPolicy;
  using dasall::infra::metrics::MetricsExporterAdapter;
  using dasall::tests::support::assert_true;

  MetricsConfigPolicy policy;
  MetricsConfigPatch profile_patch;
  profile_patch.exporter_type = std::string("prom_text");
  profile_patch.reader_interval_ms = 1000U;
  profile_patch.exporter_timeout_ms = 10000U;

  const auto config = policy.merge(profile_patch, MetricsConfigPatch{}, MetricsConfigPatch{});
  MetricReaderScheduler scheduler(config);
  MetricsExporterAdapter adapter(config);

  const auto tick = scheduler.schedule_tick(1000, make_snapshot());
  const auto batch = scheduler.pop_next_batch();
  const auto result = adapter.export_batch(*batch);

  assert_true(tick.triggered && batch.has_value() && result.ok &&
                  adapter.last_report().success_count == 3U &&
                  adapter.export_success_total() == 3U &&
                  adapter.module_snapshot().exporter_state == "prom_text" &&
                  !adapter.module_snapshot().degraded &&
                  !adapter.last_rendered_text().empty(),
              "MetricsExporterAdapter should export scheduler-produced batches through the prom_text skeleton and keep exporter state observable");
}

void test_metrics_exporter_adapter_falls_back_to_noop_on_unsupported_exporter() {
  using dasall::infra::metrics::MetricExportBatch;
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::infra::metrics::MetricsExporterAdapter;
  using dasall::infra::metrics::map_metrics_error_code;
  using dasall::tests::support::assert_true;

  MetricsExporterAdapter adapter;
  const auto result = adapter.export_batch(MetricExportBatch{
      .batch_id = std::string("metrics-batch://unsupported/1"),
      .sample_count = 2U,
      .exporter_type = std::string("otlp"),
  });

  assert_true(!result.ok && result.references_only_contract_error_types() &&
                  result.result_code ==
                      map_metrics_error_code(MetricsErrorCode::ExportFailure).result_code &&
                  adapter.export_failure_total() == 2U &&
                  adapter.module_snapshot().degraded &&
                  adapter.module_snapshot().exporter_state == "noop",
              "MetricsExporterAdapter should fall back to noop and surface export failures observably when an unsupported exporter is requested");
}

void test_metrics_exporter_adapter_reports_timeout_observably() {
  using dasall::infra::metrics::MetricExportBatch;
  using dasall::infra::metrics::MetricsConfigPatch;
  using dasall::infra::metrics::MetricsConfigPolicy;
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::infra::metrics::MetricsExporterAdapter;
  using dasall::infra::metrics::map_metrics_error_code;
  using dasall::tests::support::assert_true;

  MetricsConfigPolicy policy;
  MetricsConfigPatch profile_patch;
  profile_patch.exporter_type = std::string("prom_text");
  profile_patch.exporter_timeout_ms = 1U;

  MetricsExporterAdapter adapter(
      policy.merge(profile_patch, MetricsConfigPatch{}, MetricsConfigPatch{}));
  const auto result = adapter.export_batch(MetricExportBatch{
      .batch_id = std::string("metrics-batch://timeout/1"),
      .sample_count = 5U,
      .exporter_type = std::string("prom_text"),
  });

  assert_true(!result.ok && result.references_only_contract_error_types() &&
                  result.result_code ==
                      map_metrics_error_code(MetricsErrorCode::ExportTimeout).result_code &&
                  adapter.export_failure_total() == 5U && adapter.last_report().fail_count == 5U &&
                  adapter.last_report().latency_ms == 5.0 && adapter.module_snapshot().degraded &&
                  adapter.module_snapshot().exporter_state == "noop",
              "MetricsExporterAdapter should surface prom_text timeout failures observably and degrade back to noop");
}

}  // namespace

int main() {
  try {
    test_metrics_exporter_adapter_exports_prom_text_batches_from_reader_scheduler();
    test_metrics_exporter_adapter_falls_back_to_noop_on_unsupported_exporter();
    test_metrics_exporter_adapter_reports_timeout_observably();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}