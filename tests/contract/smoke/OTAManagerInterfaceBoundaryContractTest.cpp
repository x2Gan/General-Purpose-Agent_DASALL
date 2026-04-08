#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

#include "ota/IOTAManager.h"
#include "support/TestAssertions.h"

namespace {

class BoundaryOTAManager final : public dasall::infra::ota::IOTAManager {
 public:
  [[nodiscard]] dasall::infra::ota::PrecheckReport precheck(
      const dasall::infra::ota::UpgradePlan&) const override {
    return dasall::infra::ota::PrecheckReport{
        .health_ok = true,
        .resource_ok = true,
        .compatibility_ok = true,
        .policy_ok = true,
        .blocking_reasons = {},
    };
  }

  dasall::infra::ota::UpgradeOutcome apply(
      const dasall::infra::ota::UpgradePlan&) override {
    return dasall::infra::ota::UpgradeOutcome{
        .phase = std::string("success"),
        .result_code = std::nullopt,
        .rollback_applied = false,
        .final_version_set = {std::string("rootfs=1.2.3")},
        .evidence_ref = std::string("audit://ota/apply/shape"),
    };
  }

  dasall::infra::ota::UpgradeOutcome rollback(
      const dasall::infra::ota::RollbackToken&) override {
    return dasall::infra::ota::UpgradeOutcome{
        .phase = std::string("rollback_applied"),
        .result_code = std::nullopt,
        .rollback_applied = true,
        .final_version_set = {std::string("rootfs=rollback")},
        .evidence_ref = std::string("audit://ota/rollback/shape"),
    };
  }

  [[nodiscard]] dasall::infra::ota::OTAStatusSnapshot query_status() const override {
    return dasall::infra::ota::OTAStatusSnapshot{
        .last_plan_id = std::string("ota-plan-002"),
        .state = std::string("idle"),
        .active_slot = std::string("rootfs_a"),
        .pending_confirm = false,
        .last_failure_code = std::nullopt,
        .backlog_count = 0,
    };
  }
};

void test_ota_manager_interface_keeps_private_ota_objects_on_the_boundary() {
  using dasall::infra::ota::IOTAManager;
  using dasall::infra::ota::OTAStatusSnapshot;
  using dasall::infra::ota::PrecheckReport;
  using dasall::infra::ota::RollbackToken;
  using dasall::infra::ota::UpgradeOutcome;
  using dasall::infra::ota::UpgradePlan;

  static_assert(std::is_same_v<decltype(std::declval<const IOTAManager&>().precheck(
                                   std::declval<const UpgradePlan&>())),
                               PrecheckReport>);
  static_assert(std::is_same_v<decltype(std::declval<IOTAManager&>().apply(
                                   std::declval<const UpgradePlan&>())),
                               UpgradeOutcome>);
  static_assert(std::is_same_v<decltype(std::declval<IOTAManager&>().rollback(
                                   std::declval<const RollbackToken&>())),
                               UpgradeOutcome>);
  static_assert(std::is_same_v<decltype(std::declval<const IOTAManager&>().query_status()),
                               OTAStatusSnapshot>);
}

void test_ota_manager_interface_returns_contract_stable_failure_and_status_shapes() {
  using dasall::contracts::ResultCode;
  using dasall::tests::support::assert_true;

  BoundaryOTAManager manager;

  const auto precheck_report = manager.precheck(dasall::infra::ota::UpgradePlan{
      .plan_id = std::string("ota-plan-002"),
      .requested_by = dasall::infra::ota::UpgradeRequester{
          .actor_ref = std::string("ops-user"),
          .request_id = std::string("req-ota-002"),
      },
      .target_scope = std::string("device.local"),
      .artifact_refs = {std::string("artifact-rootfs-a")},
      .strategy = std::string("safe_switch"),
      .validate_only = false,
  });
  assert_true(precheck_report.is_valid() && precheck_report.passed(),
              "IOTAManager precheck should keep PrecheckReport as the stable non-contract boundary object");

  const auto apply_outcome = manager.apply(dasall::infra::ota::UpgradePlan{});
  assert_true(apply_outcome.is_success(),
              "IOTAManager apply should return UpgradeOutcome rather than projecting OTA state into contracts objects");

  const auto status_snapshot = manager.query_status();
  assert_true(status_snapshot.is_valid() && !status_snapshot.last_failure_code.has_value(),
              "IOTAManager query_status should keep failure summary inside optional contracts ResultCode only");

  const dasall::infra::ota::UpgradeOutcome failed_outcome{
      .phase = std::string("verify_failed"),
      .result_code = ResultCode::ValidationFieldMissing,
      .rollback_applied = false,
      .final_version_set = {},
      .evidence_ref = std::string("audit://ota/apply/fail-002"),
  };
  assert_true(failed_outcome.is_failure() && failed_outcome.references_only_contract_result_code(),
              "IOTAManager failure paths should stay inside the contracts ResultCode segment without adding shared objects");
}

}  // namespace

int main() {
  try {
    test_ota_manager_interface_keeps_private_ota_objects_on_the_boundary();
    test_ota_manager_interface_returns_contract_stable_failure_and_status_shapes();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}