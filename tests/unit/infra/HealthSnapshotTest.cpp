#include <exception>
#include <iostream>
#include <cstdint>
#include <string>

#include "health/HealthStateTypes.h"
#include "support/TestAssertions.h"

namespace {

void test_health_snapshot_distinguishes_ready_degraded_and_failed_states() {
  using dasall::infra::HealthState;
  using dasall::infra::HealthSnapshot;
  using dasall::tests::support::assert_true;

  const HealthSnapshot ready{
      .liveness = true,
      .readiness = true,
      .degraded = false,
      .failed_components = {},
    .version = 1,
    .timestamp = 1711785600000,
  };

  const HealthSnapshot degraded{
      .liveness = true,
      .readiness = true,
      .degraded = true,
      .failed_components = {"metrics_exporter"},
    .version = 2,
    .timestamp = 1711785600100,
  };

  const HealthSnapshot failed{
      .liveness = false,
      .readiness = false,
      .degraded = false,
      .failed_components = {"health_scheduler", "logging_sink"},
    .version = 3,
    .timestamp = 1711785600200,
  };

  assert_true(ready.is_ready(),
              "healthy snapshot should report ready when liveness and readiness are true without degradation");
  assert_true(ready.state() == HealthState::Healthy,
        "healthy snapshot should map to the Healthy aggregate state");
  assert_true(!ready.is_degraded_state(),
              "ready snapshot should not be treated as degraded");
  assert_true(!ready.is_failed_state(),
              "ready snapshot should not be treated as failed");
  assert_true(ready.has_version_metadata(),
        "healthy snapshot should carry version and timestamp metadata once HealthStateTypes are frozen");

  assert_true(degraded.is_degraded_state(),
              "degraded snapshot should remain alive while reporting constrained service");
  assert_true(degraded.state() == HealthState::Degraded,
        "degraded snapshot should map to the Degraded aggregate state");
  assert_true(!degraded.is_ready(),
              "degraded snapshot should not be treated as fully ready");
  assert_true(!degraded.is_failed_state(),
              "degraded snapshot should remain distinct from failed");

  assert_true(failed.is_failed_state(),
              "non-live snapshot should be classified as failed");
  assert_true(failed.state() == HealthState::Unhealthy,
        "non-live snapshot should map to the Unhealthy aggregate state");
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
      .version = 4,
      .timestamp = 1711785600300,
  };

  const HealthSnapshot empty_component{
      .liveness = true,
      .readiness = false,
      .degraded = true,
      .failed_components = {""},
      .version = 5,
      .timestamp = 1711785600400,
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
      .version = 6,
      .timestamp = 1711785600500,
  };

  const HealthSnapshot dead_but_degraded{
      .liveness = false,
      .readiness = false,
      .degraded = true,
      .failed_components = {"health_monitor"},
      .version = 7,
      .timestamp = 1711785600600,
  };

  assert_true(!dead_but_ready.has_consistent_state(),
              "non-live snapshots must not stay marked as ready");
  assert_true(!dead_but_degraded.has_consistent_state(),
              "non-live snapshots must not stay marked as degraded");
}

  void test_health_state_types_enforce_monotonic_versioning_and_transition_fields() {
    using dasall::infra::HealthSnapshot;
    using dasall::infra::HealthState;
    using dasall::infra::HealthTransition;
    using dasall::tests::support::assert_true;

    const HealthSnapshot previous{
      .liveness = true,
      .readiness = true,
      .degraded = false,
      .failed_components = {},
      .version = 8,
      .timestamp = 1711785600700,
    };

    const HealthSnapshot current{
      .liveness = true,
      .readiness = false,
      .degraded = true,
      .failed_components = {"health_scheduler"},
      .version = 9,
      .timestamp = 1711785600800,
    };

    const HealthSnapshot stale{
      .liveness = true,
      .readiness = false,
      .degraded = true,
      .failed_components = {"health_scheduler"},
      .version = 8,
      .timestamp = 1711785600900,
    };

    const HealthTransition transition{
      .from_state = HealthState::Healthy,
      .to_state = HealthState::Degraded,
      .reason = "probe timeout threshold reached",
      .trigger_probe = "health_scheduler",
      .timestamp = 1711785600800,
    };

    const HealthTransition invalid_transition{
      .from_state = HealthState::Healthy,
      .to_state = HealthState::Healthy,
      .reason = std::string(),
      .trigger_probe = std::string(),
      .timestamp = 0,
    };

    assert_true(current.is_newer_than(previous),
          "HealthSnapshot version should advance monotonically when a newer state is published");
    assert_true(!stale.is_newer_than(previous),
          "HealthSnapshot should reject non-incrementing versions even when timestamps move forward");
    assert_true(transition.has_required_fields(),
          "HealthTransition should require a real state change, reason, trigger probe, and timestamp");
    assert_true(!invalid_transition.has_required_fields(),
          "HealthTransition should reject missing transition metadata and same-state hops");
  }

}  // namespace

int main() {
  try {
    test_health_snapshot_distinguishes_ready_degraded_and_failed_states();
    test_health_snapshot_rejects_invalid_failed_component_entries();
    test_health_snapshot_rejects_impossible_state_combinations();
    test_health_state_types_enforce_monotonic_versioning_and_transition_fields();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}