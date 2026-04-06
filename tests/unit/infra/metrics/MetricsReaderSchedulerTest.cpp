#include <exception>
#include <iostream>
#include <string>

#include "metrics/AggregationEngine.h"
#include "metrics/MetricReaderScheduler.h"
#include "metrics/MetricsConfigPolicy.h"
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
          .sample_count = 2U,
          .accumulated_value = 3.0,
          .last_value = 3.0,
          .min_value = 1.0,
          .max_value = 2.0,
          .bucket_counts = {},
      });
  snapshot.metrics.emplace(
      "metrics.queue_depth",
      AggregatedMetricValue{
          .identity = MetricIdentity{
              .name = std::string("metrics.queue_depth"),
              .type = MetricType::Gauge,
              .unit = std::string("1"),
              .description = std::string("queue depth"),
          },
          .sample_count = 1U,
          .accumulated_value = 4.0,
          .last_value = 4.0,
          .min_value = 4.0,
          .max_value = 4.0,
          .bucket_counts = {},
      });
  return snapshot;
}

void test_metric_reader_scheduler_triggers_batches_on_interval() {
  using dasall::infra::metrics::MetricReaderScheduler;
  using dasall::infra::metrics::MetricsConfigPatch;
  using dasall::infra::metrics::MetricsConfigPolicy;
  using dasall::tests::support::assert_true;

  MetricsConfigPolicy policy;
  MetricsConfigPatch profile_patch;
  profile_patch.exporter_type = std::string("prom_text");
  profile_patch.reader_interval_ms = 1000U;

  MetricReaderScheduler scheduler(
      policy.merge(profile_patch, MetricsConfigPatch{}, MetricsConfigPatch{}));
  const auto snapshot = make_snapshot();

  const auto too_early = scheduler.schedule_tick(500, snapshot);
  const auto on_time = scheduler.schedule_tick(1000, snapshot);

  assert_true(!too_early.triggered && too_early.status.ok,
              "MetricReaderScheduler should wait until the configured first interval elapses");
  assert_true(on_time.triggered && !on_time.shutdown_flush && on_time.batch.is_valid() &&
                  on_time.batch.sample_count == 3U && on_time.batch.exporter_type == "prom_text" &&
                  scheduler.pending_batch_count() == 1U && scheduler.last_tick_unix_ms() == 1000,
              "MetricReaderScheduler should enqueue an export batch with the merged exporter type once the configured interval is reached");
}

void test_metric_reader_scheduler_flushes_snapshot_on_shutdown() {
  using dasall::infra::metrics::MetricReaderScheduler;
  using dasall::infra::metrics::MetricsConfigPatch;
  using dasall::infra::metrics::MetricsConfigPolicy;
  using dasall::tests::support::assert_true;

  MetricsConfigPolicy policy;
  MetricsConfigPatch profile_patch;
  profile_patch.exporter_type = std::string("noop");
  profile_patch.reader_interval_ms = 5000U;

  MetricReaderScheduler scheduler(
      policy.merge(profile_patch, MetricsConfigPatch{}, MetricsConfigPatch{}));
  const auto snapshot = make_snapshot();
  const auto flushed = scheduler.flush_on_shutdown(250, snapshot);
  const auto popped = scheduler.pop_next_batch();

  assert_true(flushed.triggered && flushed.shutdown_flush && flushed.batch.is_valid() &&
                  flushed.batch.batch_id.find("shutdown") != std::string::npos &&
                  popped.has_value() && popped->batch_id == flushed.batch.batch_id &&
                  scheduler.pending_batch_count() == 0U,
              "MetricReaderScheduler should force a final export batch during shutdown even when the periodic interval has not elapsed");
}

}  // namespace

int main() {
  try {
    test_metric_reader_scheduler_triggers_batches_on_interval();
    test_metric_reader_scheduler_flushes_snapshot_on_shutdown();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}