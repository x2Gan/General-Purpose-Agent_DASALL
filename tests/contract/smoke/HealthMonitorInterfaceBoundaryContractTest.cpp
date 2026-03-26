#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

#include "../../../infra/include/IHealthMonitor.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_health_monitor_results_use_contract_error_types_only() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::HealthEvaluationResult;
  using dasall::infra::HealthMonitorRegistrationResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(HealthMonitorRegistrationResult{}.result_code), ResultCode>);
  static_assert(std::is_same_v<decltype(HealthMonitorRegistrationResult{}.error), std::optional<ErrorInfo>>);
  static_assert(std::is_same_v<decltype(HealthEvaluationResult{}.result_code), ResultCode>);
  static_assert(std::is_same_v<decltype(HealthEvaluationResult{}.error), std::optional<ErrorInfo>>);

  const auto registration_failure = HealthMonitorRegistrationResult::failure(
      ResultCode::ValidationFieldMissing,
      "probe registration is required",
      "health.register_probe",
      "IHealthMonitor");
  const auto evaluation_failure = HealthEvaluationResult::failure(
      ResultCode::ValidationFieldMissing,
      "registered probes are required",
      "health.evaluate",
      "IHealthMonitor");

  assert_true(!registration_failure.ok,
              "health registration failures should remain explicit failures");
  assert_true(registration_failure.references_only_contract_error_types(),
              "IHealthMonitor registration path should expose only contracts ResultCode/ErrorInfo types");
  assert_true(!evaluation_failure.ok,
              "health evaluation failures should remain explicit failures");
  assert_true(evaluation_failure.references_only_contract_error_types(),
              "IHealthMonitor evaluation path should expose only contracts ResultCode/ErrorInfo types");
}

void test_health_monitor_keeps_probe_registration_local_and_outputs_health_snapshot_only() {
  using dasall::infra::HealthEvaluationResult;
  using dasall::infra::HealthProbeRegistration;
  using dasall::infra::HealthSnapshot;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(HealthProbeRegistration{}.probe_name), std::string>);
  static_assert(std::is_same_v<decltype(HealthProbeRegistration{}.probe_group), std::string>);
  static_assert(std::is_same_v<decltype(HealthProbeRegistration{}.opaque_probe_ref), std::string>);
  static_assert(std::is_same_v<decltype(HealthEvaluationResult{}.snapshot), HealthSnapshot>);

  const HealthProbeRegistration valid_registration{
      .probe_name = "config_center",
      .probe_group = "liveness",
      .opaque_probe_ref = "probe://config_center",
  };
  const HealthProbeRegistration invalid_registration{};

  assert_true(valid_registration.is_valid(),
              "non-empty placeholder probe registration should satisfy the local guard");
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