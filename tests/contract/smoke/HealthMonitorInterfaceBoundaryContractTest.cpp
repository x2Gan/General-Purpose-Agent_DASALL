#include <exception>
#include <iostream>
#include <optional>
#include <cstdint>
#include <string>
#include <type_traits>

#include "../../../infra/include/health/IHealthMonitor.h"
#include "support/TestAssertions.h"

namespace {

template <typename T>
concept HasEvaluateMethod = requires {
  &T::evaluate;
};

template <typename T>
concept HasEvaluateNowMethod = requires {
  &T::evaluate_now;
};

template <typename T>
concept HasGetSnapshotMethod = requires {
  &T::get_snapshot;
};

template <typename T>
concept HasSubscribeMethod = requires {
  &T::subscribe;
};

template <typename T>
concept HasExecutedActionMember = requires {
  &T::executed_action;
};

template <typename T>
concept HasCheckpointRefMember = requires {
  &T::checkpoint_ref;
};

[[nodiscard]] dasall::infra::IHealthProbe* make_placeholder_probe_ref() {
  return reinterpret_cast<dasall::infra::IHealthProbe*>(static_cast<std::uintptr_t>(0x1));
}

void test_health_monitor_results_use_contract_error_types_only() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::HealthListenerSubscriptionResult;
  using dasall::infra::HealthMonitorRegistrationResult;
  using dasall::infra::HealthSnapshotResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(HealthMonitorRegistrationResult{}.result_code), std::optional<ResultCode>>);
  static_assert(std::is_same_v<decltype(HealthMonitorRegistrationResult{}.error), std::optional<ErrorInfo>>);
  static_assert(std::is_same_v<decltype(HealthSnapshotResult{}.result_code), std::optional<ResultCode>>);
  static_assert(std::is_same_v<decltype(HealthSnapshotResult{}.error), std::optional<ErrorInfo>>);
  static_assert(std::is_same_v<decltype(HealthListenerSubscriptionResult{}.result_code), std::optional<ResultCode>>);
  static_assert(std::is_same_v<decltype(HealthListenerSubscriptionResult{}.error), std::optional<ErrorInfo>>);

  const auto registration_failure = HealthMonitorRegistrationResult::failure(
      ResultCode::ValidationFieldMissing,
      "probe registration is required",
      "health.register_probe",
      "IHealthMonitor");
  const auto evaluation_failure = HealthSnapshotResult::failure(
      ResultCode::ValidationFieldMissing,
      "registered probes are required",
      "health.evaluate_now",
      "IHealthMonitor");
  const auto subscription_failure = HealthListenerSubscriptionResult::failure(
      ResultCode::ValidationFieldMissing,
      "listener registration is required",
      "health.subscribe",
      "IHealthMonitor");

  assert_true(!registration_failure.ok,
              "health registration failures should remain explicit failures");
  assert_true(registration_failure.references_only_contract_error_types(),
              "IHealthMonitor registration path should expose only contracts ResultCode/ErrorInfo types");
  assert_true(!evaluation_failure.ok,
              "health evaluation failures should remain explicit failures");
  assert_true(evaluation_failure.references_only_contract_error_types(),
          "IHealthMonitor evaluate_now path should expose only contracts ResultCode/ErrorInfo types");
    assert_true(!subscription_failure.ok,
          "health listener subscription failures should remain explicit failures");
    assert_true(subscription_failure.references_only_contract_error_types(),
          "IHealthMonitor subscribe path should expose only contracts ResultCode/ErrorInfo types");
}

void test_health_monitor_keeps_probe_registration_local_and_outputs_health_snapshot_only() {
  using dasall::infra::HealthProbeRegistration;
    using dasall::infra::HealthSnapshotResult;
    using dasall::infra::IHealthMonitor;
    using dasall::infra::IHealthProbe;
    using dasall::infra::IHealthStateListener;
  using dasall::infra::HealthSnapshot;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(HealthProbeRegistration{}.probe_name), std::string>);
  static_assert(std::is_same_v<decltype(HealthProbeRegistration{}.probe_group), std::string>);
    static_assert(std::is_same_v<decltype(HealthProbeRegistration{}.probe), IHealthProbe*>);
    static_assert(std::is_same_v<decltype(HealthSnapshotResult{}.snapshot), HealthSnapshot>);
    static_assert(std::is_abstract_v<IHealthStateListener>);
    static_assert(!HasEvaluateMethod<IHealthMonitor>);
    static_assert(HasEvaluateNowMethod<IHealthMonitor>);
    static_assert(HasGetSnapshotMethod<IHealthMonitor>);
    static_assert(HasSubscribeMethod<IHealthMonitor>);
    static_assert(!HasExecutedActionMember<HealthProbeRegistration>);
    static_assert(!HasCheckpointRefMember<HealthSnapshotResult>);

  const HealthProbeRegistration valid_registration{
      .probe_name = "config_center",
      .probe_group = "liveness",
      .probe = make_placeholder_probe_ref(),
  };
  const HealthProbeRegistration invalid_registration{};

  assert_true(valid_registration.is_valid(),
          "non-empty placeholder probe registration with an interface pointer should satisfy the local guard");
  assert_true(!invalid_registration.is_valid(),
          "empty placeholder probe registration should remain invalid until probe semantics are fully designed");
}

}  // namespace

int main() {
  try {
    test_health_monitor_results_use_contract_error_types_only();
    test_health_monitor_keeps_probe_registration_local_and_outputs_health_snapshot_only();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}