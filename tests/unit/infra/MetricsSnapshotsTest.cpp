#include <cmath>
#include <exception>
#include <iostream>
#include <string>

#include "metrics/MetricsSnapshots.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_metrics_snapshots_accept_valid_export_and_health_defaults() {
  using dasall::infra::metrics::ExportBatchReport;
  using dasall::infra::metrics::MetricsModuleSnapshot;
  using dasall::tests::support::assert_true;

  const ExportBatchReport report{
      .success_count = 4,
      .fail_count = 0,
      .latency_ms = 12.5,
      .dropped_count = 0,
  };

  const MetricsModuleSnapshot snapshot{
      .queue_depth = 0,
      .guard_reject_total = 0,
      .exporter_state = std::string("noop"),
      .degraded = false,
  };

  assert_true(report.is_valid(),
              "ExportBatchReport should accept non-negative finite latency and explicit success/failure counters");
  assert_true(!report.has_failures(),
              "ExportBatchReport should remain failure-free when fail_count and dropped_count are zero");
  assert_true(snapshot.is_valid(),
              "MetricsModuleSnapshot should accept an explicit exporter_state placeholder");
  assert_true(snapshot.is_healthy(),
              "MetricsModuleSnapshot should report healthy when degraded is false");
}

void test_metrics_snapshots_keep_failure_and_degraded_guards_binary() {
  using dasall::infra::metrics::ExportBatchReport;
  using dasall::infra::metrics::MetricsModuleSnapshot;
  using dasall::tests::support::assert_true;

  const ExportBatchReport failing_report{
      .success_count = 1,
      .fail_count = 2,
      .latency_ms = 30.0,
      .dropped_count = 1,
  };

  const ExportBatchReport invalid_report{
      .success_count = 0,
      .fail_count = 0,
      .latency_ms = std::nan(""),
      .dropped_count = 0,
  };

  const MetricsModuleSnapshot degraded_snapshot{
      .queue_depth = 8,
      .guard_reject_total = 3,
      .exporter_state = std::string("degraded-noop"),
      .degraded = true,
  };

  const MetricsModuleSnapshot invalid_snapshot{
      .queue_depth = 0,
      .guard_reject_total = 0,
      .exporter_state = std::string(),
      .degraded = false,
  };

  assert_true(failing_report.has_failures(),
              "ExportBatchReport should mark failed or dropped batches as observable failures");
  assert_true(!invalid_report.is_valid(),
              "ExportBatchReport should reject non-finite latency placeholders");
  assert_true(degraded_snapshot.is_valid() && !degraded_snapshot.is_healthy(),
              "MetricsModuleSnapshot should expose degraded state without invalidating the snapshot object");
  assert_true(!invalid_snapshot.is_valid(),
              "MetricsModuleSnapshot should reject an empty exporter_state placeholder");
}

}  // namespace

int main() {
  try {
    test_metrics_snapshots_accept_valid_export_and_health_defaults();
    test_metrics_snapshots_keep_failure_and_degraded_guards_binary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}