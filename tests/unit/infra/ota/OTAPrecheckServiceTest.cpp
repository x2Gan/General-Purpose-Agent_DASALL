#include <exception>
#include <iostream>
#include <string>

#include "ota/OTAPrecheckService.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

using dasall::infra::ota::IOTAHealthSignalProvider;
using dasall::infra::ota::IOTAPrecheckPolicyProvider;
using dasall::infra::ota::IOTAResourceProbe;
using dasall::infra::ota::OTAHealthSnapshot;
using dasall::infra::ota::OTAMode;
using dasall::infra::ota::OTAPrecheckPolicy;
using dasall::infra::ota::OTAPrecheckService;
using dasall::infra::ota::OTAResourceSnapshot;
using dasall::infra::ota::UpgradePlan;
using dasall::infra::ota::UpgradeRequester;

UpgradePlan make_valid_plan(bool validate_only = false) {
  return UpgradePlan{
      .plan_id = std::string("ota-plan-006"),
      .requested_by = UpgradeRequester{
          .actor_ref = std::string("ops-user"),
          .request_id = std::string("req-ota-006"),
      },
      .target_scope = std::string("device.local"),
      .artifact_refs = {std::string("artifact-rootfs-a")},
      .strategy = std::string("safe_switch"),
      .validate_only = validate_only,
  };
}

class FakeHealthProvider final : public IOTAHealthSignalProvider {
 public:
  explicit FakeHealthProvider(OTAHealthSnapshot snapshot)
      : snapshot_(std::move(snapshot)) {}

  [[nodiscard]] OTAHealthSnapshot current_health() const override {
    return snapshot_;
  }

 private:
  OTAHealthSnapshot snapshot_;
};

class FakeResourceProbe final : public IOTAResourceProbe {
 public:
  explicit FakeResourceProbe(OTAResourceSnapshot snapshot)
      : snapshot_(snapshot) {}

  [[nodiscard]] OTAResourceSnapshot current_resources() const override {
    return snapshot_;
  }

 private:
  OTAResourceSnapshot snapshot_;
};

class FakePolicyProvider final : public IOTAPrecheckPolicyProvider {
 public:
  explicit FakePolicyProvider(OTAPrecheckPolicy policy)
      : policy_(policy) {}

  [[nodiscard]] OTAPrecheckPolicy current_policy() const override {
    return policy_;
  }

 private:
  OTAPrecheckPolicy policy_;
};

void test_ota_precheck_service_accepts_ready_apply_request() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const FakeHealthProvider health_provider(OTAHealthSnapshot{.ready = true,
                                                             .degraded = false,
                                                             .failed_components = {}});
  const FakeResourceProbe resource_probe(
      OTAResourceSnapshot{.free_space_mb = 1024, .cpu_load_pct = 24});
  const FakePolicyProvider policy_provider(OTAPrecheckPolicy{
      .enabled = true,
      .mode = OTAMode::ApplyEnabled,
      .min_free_space_mb = 256,
      .max_cpu_load_pct = 80,
      .require_health_ready = true,
      .consecutive_failures = 0,
      .freeze_after_failures = 3,
  });

  const OTAPrecheckService service(OTAPrecheckService::Dependencies{
      .health_provider = &health_provider,
      .resource_probe = &resource_probe,
      .policy_provider = &policy_provider,
  });

  const auto report = service.precheck(make_valid_plan(false));
  assert_true(report.is_valid() && report.passed(),
              "OTAPrecheckService should accept a fully specified plan when health, resource, and policy gates are satisfied");
  assert_equal(std::size_t{0}, report.blocking_reasons.size(),
               "successful ota precheck must remain side-effect free and carry no blocking reasons");
}

void test_ota_precheck_service_accepts_validate_only_in_dry_run_mode() {
  using dasall::tests::support::assert_true;

  const FakeHealthProvider health_provider(OTAHealthSnapshot{.ready = true,
                                                             .degraded = false,
                                                             .failed_components = {}});
  const FakeResourceProbe resource_probe(
      OTAResourceSnapshot{.free_space_mb = 768, .cpu_load_pct = 18});
  const FakePolicyProvider policy_provider(OTAPrecheckPolicy{
      .enabled = true,
      .mode = OTAMode::DryRun,
      .min_free_space_mb = 256,
      .max_cpu_load_pct = 80,
      .require_health_ready = true,
      .consecutive_failures = 0,
      .freeze_after_failures = 3,
  });

  const OTAPrecheckService service(OTAPrecheckService::Dependencies{
      .health_provider = &health_provider,
      .resource_probe = &resource_probe,
      .policy_provider = &policy_provider,
  });

  const auto report = service.precheck(make_valid_plan(true));
  assert_true(report.is_valid() && report.passed(),
              "validate_only should remain executable in dry_run mode because it performs no slot or boot mutation");
}

void test_ota_precheck_service_blocks_invalid_plan_health_resource_and_policy_failures() {
  using dasall::tests::support::assert_true;

  const FakeHealthProvider healthy_provider(OTAHealthSnapshot{.ready = true,
                                                              .degraded = false,
                                                              .failed_components = {}});
  const FakeResourceProbe healthy_resources(
      OTAResourceSnapshot{.free_space_mb = 1024, .cpu_load_pct = 22});
  const FakePolicyProvider apply_enabled_policy(OTAPrecheckPolicy{
      .enabled = true,
      .mode = OTAMode::ApplyEnabled,
      .min_free_space_mb = 256,
      .max_cpu_load_pct = 80,
      .require_health_ready = true,
      .consecutive_failures = 0,
      .freeze_after_failures = 3,
  });
  const OTAPrecheckService baseline_service(OTAPrecheckService::Dependencies{
      .health_provider = &healthy_provider,
      .resource_probe = &healthy_resources,
      .policy_provider = &apply_enabled_policy,
  });

  const auto invalid_plan_report = baseline_service.precheck(UpgradePlan{});
  assert_true(invalid_plan_report.is_valid() && !invalid_plan_report.passed() &&
                  !invalid_plan_report.compatibility_ok &&
                  invalid_plan_report.uses_contract_error_types_only(),
              "OTAPrecheckService should reject an unspecified plan through contract-shaped blocking reasons before apply can mutate anything");

  const FakeHealthProvider unhealthy_provider(OTAHealthSnapshot{.ready = false,
                                                                .degraded = true,
                                                                .failed_components = {std::string("health.monitor")}});
  const OTAPrecheckService health_blocked_service(OTAPrecheckService::Dependencies{
      .health_provider = &unhealthy_provider,
      .resource_probe = &healthy_resources,
      .policy_provider = &apply_enabled_policy,
  });
  const auto health_blocked_report = health_blocked_service.precheck(make_valid_plan(false));
  assert_true(health_blocked_report.is_valid() && !health_blocked_report.passed() &&
                  !health_blocked_report.health_ok &&
                  health_blocked_report.uses_contract_error_types_only(),
              "OTAPrecheckService should hard-fail apply when readiness is false and require_health_ready is enabled");

  const FakeResourceProbe constrained_resources(
      OTAResourceSnapshot{.free_space_mb = 32, .cpu_load_pct = 95});
  const OTAPrecheckService resource_blocked_service(OTAPrecheckService::Dependencies{
      .health_provider = &healthy_provider,
      .resource_probe = &constrained_resources,
      .policy_provider = &apply_enabled_policy,
  });
  const auto resource_blocked_report =
      resource_blocked_service.precheck(make_valid_plan(false));
  assert_true(resource_blocked_report.is_valid() && !resource_blocked_report.passed() &&
                  !resource_blocked_report.resource_ok &&
                  resource_blocked_report.uses_contract_error_types_only(),
              "OTAPrecheckService should block apply when free space or cpu load violates the frozen resource thresholds");

  const FakePolicyProvider denied_policy(OTAPrecheckPolicy{
      .enabled = true,
      .mode = OTAMode::DryRun,
      .min_free_space_mb = 256,
      .max_cpu_load_pct = 80,
      .require_health_ready = true,
      .consecutive_failures = 0,
      .freeze_after_failures = 3,
  });
  const OTAPrecheckService policy_blocked_service(OTAPrecheckService::Dependencies{
      .health_provider = &healthy_provider,
      .resource_probe = &healthy_resources,
      .policy_provider = &denied_policy,
  });
  const auto policy_blocked_report =
      policy_blocked_service.precheck(make_valid_plan(false));
  assert_true(policy_blocked_report.is_valid() && !policy_blocked_report.passed() &&
                  !policy_blocked_report.policy_ok &&
                  policy_blocked_report.uses_contract_error_types_only(),
              "OTAPrecheckService should block apply whenever policy keeps the device in dry_run mode");
}

}  // namespace

int main() {
  try {
    test_ota_precheck_service_accepts_ready_apply_request();
    test_ota_precheck_service_accepts_validate_only_in_dry_run_mode();
    test_ota_precheck_service_blocks_invalid_plan_health_resource_and_policy_failures();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}