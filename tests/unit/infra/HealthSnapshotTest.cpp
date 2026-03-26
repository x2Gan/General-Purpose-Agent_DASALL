#include <exception>
#include <iostream>
#include <string>

#include "HealthSnapshot.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_health_snapshot_distinguishes_ready_degraded_and_failed_states() {
  using dasall::infra::HealthSnapshot;
  using dasall::tests::support::assert_true;

  const HealthSnapshot ready{
      .liveness = true,
      .readiness = true,
      .degraded = false,
      .failed_components = {},
  };

  const HealthSnapshot degraded{
      .liveness = true,
      .readiness = true,
      .degraded = true,
      .failed_components = {"metrics_exporter"},
  };

  const HealthSnapshot failed{
      .liveness = false,
      .readiness = false,
      .degraded = false,
      .failed_components = {"health_scheduler", "logging_sink"},
  };

  assert_true(ready.is_ready(),
              "healthy snapshot should report ready when liveness and readiness are true without degradation");
  assert_true(!ready.is_degraded_state(),
              "ready snapshot should not be treated as degraded");
  assert_true(!ready.is_failed_state(),
              "ready snapshot should not be treated as failed");

  assert_true(degraded.is_degraded_state(),
              "degraded snapshot should remain alive while reporting constrained service");
  assert_true(!degraded.is_ready(),
              "degraded snapshot should not be treated as fully ready");
  assert_true(!degraded.is_failed_state(),
              "degraded snapshot should remain distinct from failed");

  assert_true(failed.is_failed_state(),
              "non-live snapshot should be classified as failed");
  assert_true(!failed.is_ready(),
              "failed snapshot must not be treated as ready");
}

void test_health_snapshot_rejects_invalid_failed_component_entries() {
  using dasall::infra::HealthSnapshot;
  using dasall::tests::support::assert_true;

  const HealthSnapshot duplicate_components{
      .liveness = true,
      .readiness = false,
      .degraded = true,
      .failed_components = {"probe_registry", "probe_registry"},
  };

  const HealthSnapshot empty_component{
      .liveness = true,
      .readiness = false,
      .degraded = true,
      .failed_components = {""},
  };

  assert_true(!duplicate_components.failed_components_are_valid(),
              "failed components should reject duplicate entries");
  assert_true(!empty_component.failed_components_are_valid(),
              "failed components should reject empty component names");
}

void test_health_snapshot_rejects_impossible_state_combinations() {
  using dasall::infra::HealthSnapshot;
  using dasall::tests::support::assert_true;

  const HealthSnapshot dead_but_ready{
      .liveness = false,
      .readiness = true,
      .degraded = false,
      .failed_components = {"config_center"},
  };

  const HealthSnapshot dead_but_degraded{
      .liveness = false,
      .readiness = false,
      .degraded = true,
      .failed_components = {"health_monitor"},
  };

  assert_true(!dead_but_ready.has_consistent_state(),
              "non-live snapshots must not stay marked as ready");
  assert_true(!dead_but_degraded.has_consistent_state(),
              "non-live snapshots must not stay marked as degraded");
}

}  // namespace

int main() {
  try {
    test_health_snapshot_distinguishes_ready_degraded_and_failed_states();
    test_health_snapshot_rejects_invalid_failed_component_entries();
    test_health_snapshot_rejects_impossible_state_combinations();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}