#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

#include "health/IHealthMonitor.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

class NullHealthMonitor final : public dasall::infra::IHealthMonitor {
 public:
  dasall::infra::HealthMonitorRegistrationResult register_probe(
      const dasall::infra::HealthProbeRegistration& registration) override {
    if (!registration.is_valid()) {
      return dasall::infra::HealthMonitorRegistrationResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "health probe registration must stay explicitly specified",
          "health.register_probe",
          "NullHealthMonitor");
    }

    probe_registered_ = true;
    return dasall::infra::HealthMonitorRegistrationResult::success();
  }

  dasall::infra::HealthSnapshotResult evaluate_now() override {
    if (!probe_registered_) {
      return dasall::infra::HealthSnapshotResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "health monitor requires at least one registered probe placeholder",
          "health.evaluate_now",
          "NullHealthMonitor");
    }

    snapshot_ = dasall::infra::HealthSnapshot{
        .liveness = true,
        .readiness = true,
        .degraded = false,
        .failed_components = {},
    };
    has_snapshot_ = true;
    return dasall::infra::HealthSnapshotResult::success(snapshot_);
  }

  [[nodiscard]] dasall::infra::HealthSnapshotResult get_snapshot() const override {
    if (!has_snapshot_) {
      return dasall::infra::HealthSnapshotResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "health monitor should expose an explicit failure before any snapshot exists",
          "health.get_snapshot",
          "NullHealthMonitor");
    }

    return dasall::infra::HealthSnapshotResult::success(snapshot_);
  }

  dasall::infra::HealthListenerSubscriptionResult subscribe(
      dasall::infra::IHealthStateListener& listener) override {
    listener_ = &listener;
    return dasall::infra::HealthListenerSubscriptionResult::success();
  }

 private:
  bool probe_registered_ = false;
  bool has_snapshot_ = false;
  dasall::infra::HealthSnapshot snapshot_{};
  dasall::infra::IHealthStateListener* listener_ = nullptr;
};

class NullHealthStateListener final : public dasall::infra::IHealthStateListener {
 public:
  void on_health_transition(const dasall::infra::HealthTransition&,
                            const dasall::infra::HealthSnapshot&) override {
    ++notification_count_;
  }

  [[nodiscard]] int notification_count() const {
    return notification_count_;
  }

 private:
  int notification_count_ = 0;
};

[[nodiscard]] dasall::infra::IHealthProbe* make_placeholder_probe_ref() {
  return reinterpret_cast<dasall::infra::IHealthProbe*>(static_cast<std::uintptr_t>(0x1));
}

void test_health_monitor_interface_accepts_probe_registration_and_evaluation() {
  using dasall::infra::HealthProbeRegistration;
  using dasall::tests::support::assert_true;

  NullHealthMonitor monitor;
  NullHealthStateListener listener;

  const auto listener_result = monitor.subscribe(listener);
  assert_true(listener_result.ok,
              "IHealthMonitor skeleton should accept a minimal listener placeholder before transition details are frozen");

  const auto registration_result = monitor.register_probe(HealthProbeRegistration{
      .probe_name = std::string("logging_sink"),
      .probe_group = std::string("readiness"),
      .probe = make_placeholder_probe_ref(),
  });
  assert_true(registration_result.ok,
              "IHealthMonitor skeleton should accept a fully specified placeholder probe registration");

  const auto evaluation_result = monitor.evaluate_now();
  assert_true(evaluation_result.ok,
              "IHealthMonitor skeleton should evaluate_now to a HealthSnapshot after registration");
  assert_true(evaluation_result.snapshot.is_ready(),
              "placeholder evaluation should prove that HealthSnapshot remains the output boundary");

  const auto latest_snapshot = monitor.get_snapshot();
  assert_true(latest_snapshot.ok,
              "IHealthMonitor skeleton should expose the last computed snapshot through get_snapshot");
  assert_true(latest_snapshot.snapshot.is_ready(),
              "get_snapshot should surface the same readiness boundary as evaluate_now");
}

void test_health_monitor_interface_reports_validation_failures_observably() {
  using dasall::infra::HealthProbeRegistration;
  using dasall::tests::support::assert_true;

  NullHealthMonitor monitor;

  const auto registration_result = monitor.register_probe(HealthProbeRegistration{});
  assert_true(!registration_result.ok,
              "IHealthMonitor skeleton should reject an unspecified probe registration placeholder");
  assert_true(registration_result.references_only_contract_error_types(),
              "health registration validation failures should stay within contracts error types");

  const auto evaluation_result = monitor.evaluate_now();
  assert_true(!evaluation_result.ok,
              "IHealthMonitor skeleton should reject evaluate_now before any probe placeholder is registered");
  assert_true(evaluation_result.references_only_contract_error_types(),
              "health evaluation validation failures should stay within contracts error types");

  const auto snapshot_result = monitor.get_snapshot();
  assert_true(!snapshot_result.ok,
              "IHealthMonitor skeleton should reject get_snapshot before any successful evaluation has completed");
  assert_true(snapshot_result.references_only_contract_error_types(),
              "get_snapshot failure paths should stay within contracts error types");
}

}  // namespace

int main() {
  try {
    test_health_monitor_interface_accepts_probe_registration_and_evaluation();
    test_health_monitor_interface_reports_validation_failures_observably();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}