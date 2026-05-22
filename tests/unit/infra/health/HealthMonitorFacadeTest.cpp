#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
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

class MutableHealthProbe final : public dasall::infra::IHealthProbe {
 public:
  explicit MutableHealthProbe(dasall::infra::ProbeResult result)
      : result_(std::move(result)) {}

  [[nodiscard]] dasall::infra::ProbeResult probe() override {
    return result_;
  }

  void set_result(dasall::infra::ProbeResult result) {
    result_ = std::move(result);
  }

 private:
  dasall::infra::ProbeResult result_;
};

class RecordingHealthStateListener final : public dasall::infra::IHealthStateListener {
 public:
  void on_health_transition(const dasall::infra::HealthTransition& transition,
                            const dasall::infra::HealthSnapshot& snapshot) override {
    ++notification_count_;
    last_transition_ = transition;
    last_snapshot_ = snapshot;
  }

  [[nodiscard]] int notification_count() const {
    return notification_count_;
  }

  [[nodiscard]] const std::optional<dasall::infra::HealthTransition>& last_transition() const {
    return last_transition_;
  }

  [[nodiscard]] const std::optional<dasall::infra::HealthSnapshot>& last_snapshot() const {
    return last_snapshot_;
  }

 private:
  int notification_count_ = 0;
  std::optional<dasall::infra::HealthTransition> last_transition_;
  std::optional<dasall::infra::HealthSnapshot> last_snapshot_;
};

[[nodiscard]] dasall::infra::ProbeResult make_probe_result(
    std::string probe_name,
    dasall::infra::ProbeStatus status,
    std::optional<dasall::contracts::ResultCode> error_code = std::nullopt,
    std::string detail_ref = std::string()) {
  return dasall::infra::ProbeResult{
      .probe_name = std::move(probe_name),
      .status = status,
      .latency_ms = 3,
      .error_code = error_code,
      .detail_ref = std::move(detail_ref),
      .timestamp = 1712361600000,
  };
}

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

void test_health_monitor_facade_evaluates_registered_probe_results_and_notifies_transition() {
  using dasall::contracts::ResultCode;
  using dasall::infra::HealthMonitorFacade;
  using dasall::infra::HealthProbeRegistration;
  using dasall::infra::ProbeStatus;
  using dasall::tests::support::assert_true;

  HealthMonitorFacade facade;
  RecordingHealthStateListener listener;
  MutableHealthProbe probe(make_probe_result("logging_sink", ProbeStatus::Healthy));

  const auto subscribe_result = facade.subscribe(listener);
  assert_true(subscribe_result.ok,
              "HealthMonitorFacade should accept transition listeners before evaluation begins");

  const auto registration_result = facade.register_probe(HealthProbeRegistration{
      .probe_name = std::string("logging_sink"),
      .probe_group = std::string("readiness"),
      .probe = &probe,
  });
  assert_true(registration_result.ok,
              "HealthMonitorFacade should accept a mutable readiness probe for transition testing");

  const auto first_snapshot = facade.evaluate_now();
  assert_true(first_snapshot.ok && first_snapshot.snapshot.is_ready() &&
                  listener.notification_count() == 0,
              "HealthMonitorFacade should not emit a transition on the first healthy baseline snapshot");

  probe.set_result(make_probe_result("logging_sink",
                                     ProbeStatus::Degraded,
                                     ResultCode::ProviderTimeout,
                                     "health://probe/failure/logging_sink"));
  const auto second_snapshot = facade.evaluate_now();

  assert_true(second_snapshot.ok && second_snapshot.snapshot.is_degraded_state() &&
                  second_snapshot.snapshot.failed_components.size() == 1U &&
                  second_snapshot.snapshot.failed_components.front() == "logging_sink",
              "HealthMonitorFacade should aggregate the current probe result instead of returning a placeholder snapshot");
  assert_true(second_snapshot.snapshot.version > first_snapshot.snapshot.version,
              "HealthMonitorFacade should advance snapshot version metadata across evaluations");
  assert_true(listener.notification_count() == 1 && listener.last_transition().has_value() &&
                  listener.last_snapshot().has_value(),
              "HealthMonitorFacade should notify listeners when a registered probe drives a state transition");
  assert_true(listener.last_transition()->from_state == dasall::infra::HealthState::Healthy &&
                  listener.last_transition()->to_state == dasall::infra::HealthState::Degraded &&
                  listener.last_transition()->trigger_probe == "logging_sink",
              "HealthMonitorFacade should propagate structured transition metadata to listeners");
  assert_true(listener.last_snapshot()->is_degraded_state() &&
                  listener.last_snapshot()->failed_components.front() == "logging_sink",
              "HealthMonitorFacade should publish the current aggregate snapshot alongside the transition event");
}

}  // namespace

int main() {
  try {
    test_health_monitor_facade_rejects_uninitialized_paths();
    test_health_monitor_facade_registers_probe_and_evaluates_snapshot();
    test_health_monitor_facade_preserves_last_snapshot_in_safe_observe_mode();
    test_health_monitor_facade_evaluates_registered_probe_results_and_notifies_transition();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}