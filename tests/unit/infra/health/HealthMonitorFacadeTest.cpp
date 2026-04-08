#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

#include "health/HealthMonitorFacade.h"
#include "support/TestAssertions.h"

namespace {

class StaticHealthProbe final : public dasall::infra::IHealthProbe {
 public:
  [[nodiscard]] dasall::infra::ProbeResult probe() override {
    return dasall::infra::ProbeResult{
        .probe_name = std::string("logging_sink"),
        .status = dasall::infra::ProbeStatus::Healthy,
        .latency_ms = 3,
        .error_code = std::nullopt,
        .detail_ref = std::string(),
        .timestamp = 1712361600000,
    };
  }
};

class RecordingHealthStateListener final : public dasall::infra::IHealthStateListener {
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

void test_health_monitor_facade_rejects_uninitialized_paths() {
  using dasall::contracts::ResultCode;
  using dasall::infra::HealthMonitorFacade;
  using dasall::infra::HealthProbeRegistration;
  using dasall::tests::support::assert_true;

  HealthMonitorFacade facade;
  RecordingHealthStateListener listener;

  const auto subscribe_result = facade.subscribe(listener);
  assert_true(subscribe_result.ok && facade.listener_count() == 1U,
              "HealthMonitorFacade should accept listener subscription before the first evaluation runs");

  const auto invalid_registration = facade.register_probe(HealthProbeRegistration{});
  assert_true(!invalid_registration.ok && invalid_registration.references_only_contract_error_types(),
              "HealthMonitorFacade should reject unspecified probe registrations through the contracts error boundary");

  const auto evaluation_result = facade.evaluate_now();
  assert_true(!evaluation_result.ok && evaluation_result.references_only_contract_error_types() &&
                  evaluation_result.result_code.has_value() &&
                  *evaluation_result.result_code == ResultCode::ValidationFieldMissing,
              "HealthMonitorFacade should reject evaluate_now before a probe registration has initialized the facade");

  const auto snapshot_result = facade.get_snapshot();
  assert_true(!snapshot_result.ok && snapshot_result.references_only_contract_error_types() &&
                  snapshot_result.result_code.has_value() &&
                  *snapshot_result.result_code == ResultCode::ValidationFieldMissing,
              "HealthMonitorFacade should reject get_snapshot before any successful evaluation has produced a snapshot");
}

void test_health_monitor_facade_registers_probe_and_evaluates_snapshot() {
  using dasall::infra::HealthMonitorFacade;
  using dasall::infra::HealthProbeRegistration;
  using dasall::tests::support::assert_true;

  HealthMonitorFacade facade;
  StaticHealthProbe probe;

  const auto registration_result = facade.register_probe(HealthProbeRegistration{
      .probe_name = std::string("logging_sink"),
      .probe_group = std::string("readiness"),
      .probe = &probe,
  });
  assert_true(registration_result.ok && !registration_result.replaced_existing && facade.is_ready() &&
                  facade.registered_probe_count() == 1U,
              "HealthMonitorFacade should transition to ready after the first valid probe registration");

  const auto evaluation_result = facade.evaluate_now();
  assert_true(evaluation_result.ok && evaluation_result.snapshot.is_ready() &&
                  evaluation_result.snapshot.has_version_metadata(),
              "HealthMonitorFacade should generate a versioned healthy snapshot once initialized");

  const auto latest_snapshot = facade.get_snapshot();
  assert_true(latest_snapshot.ok && latest_snapshot.snapshot.version == evaluation_result.snapshot.version &&
                  latest_snapshot.snapshot.timestamp == evaluation_result.snapshot.timestamp,
              "HealthMonitorFacade should expose the latest successful snapshot through get_snapshot");
}

void test_health_monitor_facade_preserves_last_snapshot_in_safe_observe_mode() {
  using dasall::contracts::ResultCode;
  using dasall::infra::HealthMonitorFacade;
  using dasall::infra::HealthProbeRegistration;
  using dasall::tests::support::assert_true;

  HealthMonitorFacade facade;
  StaticHealthProbe probe;

  assert_true(facade.register_probe(HealthProbeRegistration{
                  .probe_name = std::string("config_center"),
                  .probe_group = std::string("liveness"),
                  .probe = &probe,
              }).ok,
              "HealthMonitorFacade should accept a valid probe before safe_observe_mode is exercised");

  const auto first_snapshot = facade.evaluate_now();
  assert_true(first_snapshot.ok,
              "HealthMonitorFacade should complete one successful evaluation before safe_observe_mode is injected");

  facade.enter_safe_observe_mode_for_test("scheduler thread fault");
  assert_true(facade.is_in_safe_observe_mode() && facade.safe_observe_reason().has_value(),
              "HealthMonitorFacade should expose safe_observe_mode once the scheduler fault hook is triggered");

  const auto blocked_evaluation = facade.evaluate_now();
  assert_true(!blocked_evaluation.ok && blocked_evaluation.references_only_contract_error_types() &&
                  blocked_evaluation.result_code.has_value() &&
                  *blocked_evaluation.result_code == ResultCode::RuntimeRetryExhausted &&
                  blocked_evaluation.error.has_value() &&
                  blocked_evaluation.error->details.message.find("safe_observe_mode") != std::string::npos,
              "HealthMonitorFacade should reject new evaluations while safe_observe_mode is active");

  const auto preserved_snapshot = facade.get_snapshot();
  assert_true(preserved_snapshot.ok &&
                  preserved_snapshot.snapshot.version == first_snapshot.snapshot.version,
              "HealthMonitorFacade should preserve the last successful snapshot while safe_observe_mode is active");
}

}  // namespace

int main() {
  try {
    test_health_monitor_facade_rejects_uninitialized_paths();
    test_health_monitor_facade_registers_probe_and_evaluates_snapshot();
    test_health_monitor_facade_preserves_last_snapshot_in_safe_observe_mode();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}