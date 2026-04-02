#include <exception>
#include <iostream>

#include "watchdog/WatchdogSnapshot.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_watchdog_snapshot_accepts_consistent_counts_and_monotonic_versions() {
  using dasall::infra::watchdog::WatchdogSnapshot;
  using dasall::tests::support::assert_true;

  const WatchdogSnapshot previous{
      .version = 1,
      .total_entities = 12,
      .timed_out_entities = 1,
      .degraded_entities = 2,
      .scan_lag_ms = 50,
      .ts = 1711958400000,
  };
  const WatchdogSnapshot current{
      .version = 2,
      .total_entities = 12,
      .timed_out_entities = 2,
      .degraded_entities = 3,
      .scan_lag_ms = 75,
      .ts = 1711958400500,
  };

  assert_true(current.has_consistent_counts(),
              "WatchdogSnapshot should require non-negative lag, bounded counters, positive timestamp, and a version");
  assert_true(current.is_newer_than(previous),
              "WatchdogSnapshot should let unit tests enforce monotonic version progression");
}

void test_watchdog_snapshot_rejects_inconsistent_counts_and_non_monotonic_versions() {
  using dasall::infra::watchdog::WatchdogSnapshot;
  using dasall::tests::support::assert_true;

  const WatchdogSnapshot previous{
      .version = 2,
      .total_entities = 8,
      .timed_out_entities = 1,
      .degraded_entities = 1,
      .scan_lag_ms = 40,
      .ts = 1711958401000,
  };
  const WatchdogSnapshot invalid{
      .version = 2,
      .total_entities = 4,
      .timed_out_entities = 5,
      .degraded_entities = 1,
      .scan_lag_ms = -1,
      .ts = 0,
  };

  assert_true(!invalid.has_consistent_counts(),
              "WatchdogSnapshot should reject negative lag, zero timestamp, or counters that exceed total_entities");
  assert_true(!invalid.is_newer_than(previous),
              "WatchdogSnapshot should reject non-monotonic version progression");
}

}  // namespace

int main() {
  try {
    test_watchdog_snapshot_accepts_consistent_counts_and_monotonic_versions();
    test_watchdog_snapshot_rejects_inconsistent_counts_and_non_monotonic_versions();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}