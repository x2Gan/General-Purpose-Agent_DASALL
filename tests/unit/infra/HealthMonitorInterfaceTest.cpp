#include <exception>
#include <iostream>
#include <string>

#include "IHealthMonitor.h"
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

  dasall::infra::HealthEvaluationResult evaluate() override {
    if (!probe_registered_) {
      return dasall::infra::HealthEvaluationResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "health monitor requires at least one registered probe placeholder",
          "health.evaluate",
          "NullHealthMonitor");
    }

    return dasall::infra::HealthEvaluationResult::success(dasall::infra::HealthSnapshot{
        .liveness = true,
        .readiness = true,
        .degraded = false,
        .failed_components = {},
    });
  }

 private:
  bool probe_registered_ = false;
};

void test_health_monitor_interface_accepts_probe_registration_and_evaluation() {
  using dasall::infra::HealthProbeRegistration;
  using dasall::tests::support::assert_true;

  NullHealthMonitor monitor;

  const auto registration_result = monitor.register_probe(HealthProbeRegistration{
      .probe_name = std::string("logging_sink"),
      .probe_group = std::string("readiness"),
      .opaque_probe_ref = std::string("probe://logging_sink"),
  });
  assert_true(registration_result.ok,
              "IHealthMonitor skeleton should accept a fully specified placeholder probe registration");

  const auto evaluation_result = monitor.evaluate();
  assert_true(evaluation_result.ok,
              "IHealthMonitor skeleton should evaluate to a HealthSnapshot after registration");
  assert_true(evaluation_result.snapshot.is_ready(),
              "placeholder evaluation should prove that HealthSnapshot remains the output boundary");
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

  const auto evaluation_result = monitor.evaluate();
  assert_true(!evaluation_result.ok,
              "IHealthMonitor skeleton should reject evaluation before any probe placeholder is registered");
  assert_true(evaluation_result.references_only_contract_error_types(),
              "health evaluation validation failures should stay within contracts error types");
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