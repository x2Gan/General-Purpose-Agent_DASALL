#include <exception>
#include <iostream>
#include <string>

#include "metrics/MetricReaderScheduler.h"
#include "metrics/MetricsConfigPolicy.h"
#include "metrics/MetricsExporterAdapter.h"
#include "metrics/MetricsFacade.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::infra::metrics::MeterScope make_scope() {
  return dasall::infra::metrics::MeterScope{
      .name = std::string("infra.metrics"),
      .version = std::string("1.0.0"),
      .schema_url = std::string("https://opentelemetry.io/schemas/1.26.0"),
  };
}

[[nodiscard]] dasall::infra::metrics::MetricsProviderConfig make_provider_config() {
  return dasall::infra::metrics::MetricsProviderConfig{
      .enabled = true,
      .provider_type = std::string("internal"),
      .exporter_type = std::string("prom_text"),
      .reader_interval_ms = 1000,
      .exporter_timeout_ms = 10000,
  };
}

[[nodiscard]] dasall::infra::metrics::MetricsResolvedConfig make_runtime_config() {
  using dasall::infra::metrics::MetricsConfigPatch;
  using dasall::infra::metrics::MetricsConfigPolicy;

  MetricsConfigPolicy policy;
  MetricsConfigPatch profile_patch;
  profile_patch.exporter_type = std::string("prom_text");
  profile_patch.reader_interval_ms = 1000U;
  profile_patch.exporter_timeout_ms = 10000U;
  return policy.merge(profile_patch, MetricsConfigPatch{}, MetricsConfigPatch{});
}

void record_sample(dasall::infra::metrics::IMeter& meter,
                   const dasall::infra::metrics::MetricIdentity& identity,
                   double value,
                   std::int64_t ts_unix_ms) {
  using dasall::infra::metrics::MetricLabels;
  using dasall::infra::metrics::MetricSample;
  using dasall::tests::support::assert_true;

  const auto result = meter.record(MetricSample{
      .identity_ref = identity,
      .value = value,
      .ts_unix_ms = ts_unix_ms,
      .labels = MetricLabels{
          .module = std::string("metrics"),
          .stage = std::string("export"),
          .profile = std::string("desktop_full"),
          .outcome = std::string("success"),
          .error_code = std::string(),
      },
  });

  assert_true(result.ok,
              "metrics integration should record valid samples through MetricsFacade before export scheduling");
}

void test_metrics_integration_records_aggregates_and_exports_batches() {
  using dasall::infra::metrics::MetricIdentity;
  using dasall::infra::metrics::MetricReaderScheduler;
  using dasall::infra::metrics::MetricsCallDeadline;
  using dasall::infra::metrics::MetricsExporterAdapter;
  using dasall::infra::metrics::MetricsFacade;
  using dasall::infra::metrics::MetricType;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  MetricsFacade facade;
  const auto init_result = facade.init(make_provider_config());
  assert_true(init_result.ok,
              "metrics integration should initialize MetricsFacade with the frozen prom_text provider skeleton");

  const auto meter = facade.get_meter(make_scope());
  assert_true(static_cast<bool>(meter),
              "metrics integration should resolve the frozen infra.metrics meter scope");

  const MetricIdentity counter_identity{
      .name = std::string("metrics_export_total"),
      .type = MetricType::Counter,
      .unit = std::string("1"),
      .description = std::string("successful metrics exports"),
  };
  const MetricIdentity latency_identity{
      .name = std::string("metrics_export_latency_ms"),
      .type = MetricType::Histogram,
      .unit = std::string("ms"),
      .description = std::string("metrics export latency"),
  };

  const auto counter_handle = meter->create_counter(counter_identity);
  const auto latency_handle = meter->create_histogram(latency_identity);
  assert_true(counter_handle.has_value() && latency_handle.has_value(),
              "metrics integration should materialize counter and histogram handles before recording samples");

  record_sample(*meter, counter_identity, 1.0, 1712563200000);
  record_sample(*meter, latency_identity, 8.0, 1712563200001);

  const auto snapshot = facade.aggregation_snapshot();
  const auto* counter_aggregate = snapshot.find("metrics_export_total");
  const auto* latency_aggregate = snapshot.find("metrics_export_latency_ms");
  assert_true(counter_aggregate != nullptr && latency_aggregate != nullptr,
              "metrics integration should expose recorded counter and histogram aggregates in the snapshot");
  assert_equal(1,
               static_cast<int>(counter_aggregate->sample_count),
               "metrics integration should preserve the counter sample count in the aggregate snapshot");
  assert_equal(1,
               static_cast<int>(latency_aggregate->sample_count),
               "metrics integration should preserve the histogram sample count in the aggregate snapshot");

  MetricReaderScheduler scheduler(make_runtime_config());
  const auto tick = scheduler.schedule_tick(1000, snapshot);
  const auto batch = scheduler.pop_next_batch();
  assert_true(tick.triggered && batch.has_value() && batch->sample_count == 2U,
              "metrics integration should turn the aggregate snapshot into one export batch with both recorded samples");

  MetricsExporterAdapter adapter(make_runtime_config());
  const auto export_result = adapter.export_batch(*batch);
  const auto facade_flush_result = facade.force_flush(MetricsCallDeadline{.timeout_ms = 100});
  const auto exporter_flush_result = adapter.force_flush(MetricsCallDeadline{.timeout_ms = 100});
  const auto facade_shutdown_result = facade.shutdown(MetricsCallDeadline{.timeout_ms = 100});
  const auto exporter_shutdown_result = adapter.shutdown(MetricsCallDeadline{.timeout_ms = 100});

  assert_true(export_result.ok && facade_flush_result.ok && exporter_flush_result.ok &&
                  facade_shutdown_result.ok && exporter_shutdown_result.ok,
              "metrics integration should keep the record -> aggregate -> export -> flush -> shutdown path executable end-to-end");
  assert_true(adapter.last_report().success_count == 2U &&
                  adapter.export_success_total() == 2U &&
                  adapter.module_snapshot().exporter_state == "stopped" &&
                  !adapter.last_rendered_text().empty(),
              "metrics integration should keep exporter success counts observable and render prom_text output before shutdown");
  assert_true(facade.last_recorded_sample().has_value() &&
                  facade.last_recorded_sample()->labels.error_code == "none" &&
                  facade.module_snapshot().exporter_state == "stopped",
              "metrics integration should normalize error_code labels and leave the provider in a stopped terminal state after shutdown");
}

}  // namespace

int main() {
  try {
    test_metrics_integration_records_aggregates_and_exports_batches();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}