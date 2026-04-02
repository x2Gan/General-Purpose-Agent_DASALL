#include <exception>
#include <iostream>
#include <string>

#include "ota/IOTAManager.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::contracts::ErrorInfo make_manager_error(int code, std::string message) {
  return dasall::contracts::ErrorInfo{
      .failure_type = dasall::contracts::ResultCodeCategory::Validation,
      .retryable = false,
      .safe_to_replan = false,
      .details = dasall::contracts::ErrorDetails{
          .code = code,
          .message = std::move(message),
          .stage = std::string("ota.manager"),
      },
      .source_ref = dasall::contracts::ErrorSourceRefMinimal{
          .ref_type = std::string("infra.ota"),
          .ref_id = std::string("NullOTAManager"),
      },
  };
}

dasall::infra::ota::UpgradePlan make_valid_plan(bool validate_only) {
  return dasall::infra::ota::UpgradePlan{
      .plan_id = std::string("ota-plan-002"),
      .requested_by = dasall::infra::ota::UpgradeRequester{
          .actor_ref = std::string("ops-user"),
          .request_id = std::string("req-ota-002"),
      },
      .target_scope = std::string("device.local"),
      .artifact_refs = {std::string("artifact-rootfs-a")},
      .strategy = std::string("safe_switch"),
      .validate_only = validate_only,
  };
}

dasall::infra::ota::RollbackToken make_valid_token() {
  return dasall::infra::ota::RollbackToken{
      .rollback_id = std::string("rollback-002"),
      .previous_boot_target = std::string("rootfs_a"),
      .staged_artifacts = {std::string("artifact-rootfs-a")},
      .created_at = std::string("2026-04-01T10:00:00Z"),
      .expires_at = std::string("2026-04-02T10:00:00Z"),
  };
}

class NullOTAManager final : public dasall::infra::ota::IOTAManager {
 public:
  [[nodiscard]] dasall::infra::ota::PrecheckReport precheck(
      const dasall::infra::ota::UpgradePlan& plan) const override {
    if (!plan.is_valid()) {
      return dasall::infra::ota::PrecheckReport{
          .health_ok = true,
          .resource_ok = true,
          .compatibility_ok = true,
          .policy_ok = false,
          .blocking_reasons = {make_manager_error(1001, "upgrade plan must stay fully specified")},
      };
    }

    return dasall::infra::ota::PrecheckReport{
        .health_ok = true,
        .resource_ok = true,
        .compatibility_ok = true,
        .policy_ok = true,
        .blocking_reasons = {},
    };
  }

  dasall::infra::ota::UpgradeOutcome apply(
      const dasall::infra::ota::UpgradePlan& plan) override {
    const auto precheck_result = precheck(plan);
    if (!precheck_result.is_valid() || !precheck_result.passed()) {
      status_snapshot_.last_plan_id = plan.plan_id;
      status_snapshot_.state = std::string("precheck_failed");
      status_snapshot_.last_failure_code = dasall::contracts::ResultCode::ValidationFieldMissing;
      return dasall::infra::ota::UpgradeOutcome{
          .phase = std::string("precheck_failed"),
          .result_code = dasall::contracts::ResultCode::ValidationFieldMissing,
          .rollback_applied = false,
          .final_version_set = {},
          .evidence_ref = std::string("audit://ota/precheck/fail-002"),
      };
    }

    status_snapshot_.last_plan_id = plan.plan_id;
    status_snapshot_.state = plan.validate_only ? std::string("validated")
                                                : std::string("applied");
    status_snapshot_.active_slot = std::string("rootfs_b");
    status_snapshot_.pending_confirm = !plan.validate_only;
    status_snapshot_.last_failure_code = std::nullopt;

    return dasall::infra::ota::UpgradeOutcome{
        .phase = plan.validate_only ? std::string("validated") : std::string("success"),
        .result_code = std::nullopt,
        .rollback_applied = false,
        .final_version_set =
            plan.validate_only ? std::vector<std::string>{}
                               : std::vector<std::string>{std::string("rootfs=1.2.3")},
        .evidence_ref = plan.validate_only ? std::string("audit://ota/validate/002")
                                           : std::string("audit://ota/apply/002"),
    };
  }

  dasall::infra::ota::UpgradeOutcome rollback(
      const dasall::infra::ota::RollbackToken& token) override {
    if (!token.is_valid()) {
      status_snapshot_.state = std::string("rollback_failed");
      status_snapshot_.last_failure_code = dasall::contracts::ResultCode::ValidationFieldMissing;
      return dasall::infra::ota::UpgradeOutcome{
          .phase = std::string("rollback_failed"),
          .result_code = dasall::contracts::ResultCode::ValidationFieldMissing,
          .rollback_applied = false,
          .final_version_set = {},
          .evidence_ref = std::string("audit://ota/rollback/fail-002"),
      };
    }

    status_snapshot_.state = std::string("rollback_applied");
    status_snapshot_.active_slot = token.previous_boot_target;
    status_snapshot_.pending_confirm = false;
    status_snapshot_.last_failure_code = std::nullopt;

    return dasall::infra::ota::UpgradeOutcome{
        .phase = std::string("rollback_applied"),
        .result_code = std::nullopt,
        .rollback_applied = true,
        .final_version_set = {std::string("rootfs=rollback")},
        .evidence_ref = std::string("audit://ota/rollback/002"),
    };
  }

  [[nodiscard]] dasall::infra::ota::OTAStatusSnapshot query_status() const override {
    return status_snapshot_;
  }

 private:
  dasall::infra::ota::OTAStatusSnapshot status_snapshot_{
      .last_plan_id = std::string("none"),
      .state = std::string("idle"),
      .active_slot = std::string("rootfs_a"),
      .pending_confirm = false,
      .last_failure_code = std::nullopt,
      .backlog_count = 0,
  };
};

void test_ota_manager_interface_accepts_precheck_apply_rollback_and_query() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  NullOTAManager manager;

  const auto precheck_report = manager.precheck(make_valid_plan(false));
  assert_true(precheck_report.is_valid() && precheck_report.passed(),
              "IOTAManager should expose a side-effect-free precheck boundary before apply");

  const auto validate_only_outcome = manager.apply(make_valid_plan(true));
  assert_true(validate_only_outcome.is_success() && validate_only_outcome.phase == "validated",
              "IOTAManager validate_only apply should freeze the validated outcome without slot mutation semantics");

  const auto apply_outcome = manager.apply(make_valid_plan(false));
  assert_true(apply_outcome.is_success() && apply_outcome.phase == "success",
              "IOTAManager apply should return UpgradeOutcome as the stable success boundary");

  const auto status_snapshot = manager.query_status();
  assert_true(status_snapshot.is_valid(),
              "IOTAManager query_status should expose a pure OTAStatusSnapshot boundary");
  assert_equal(true, status_snapshot.pending_confirm,
               "successful apply should leave pending_confirm explicit in the frozen status snapshot");

  const auto rollback_outcome = manager.rollback(make_valid_token());
  assert_true(rollback_outcome.is_success() && rollback_outcome.rollback_applied,
              "IOTAManager rollback should surface rollback_applied explicitly in UpgradeOutcome");
}

void test_ota_manager_interface_reports_invalid_inputs_with_contract_shaped_failures() {
  using dasall::tests::support::assert_true;

  NullOTAManager manager;

  const auto precheck_report = manager.precheck(dasall::infra::ota::UpgradePlan{});
  assert_true(precheck_report.is_valid() && !precheck_report.passed(),
              "IOTAManager should reject an unspecified plan through PrecheckReport blocking reasons");
  assert_true(precheck_report.uses_contract_error_types_only(),
              "IOTAManager precheck failures should stay within contracts ErrorInfo semantics");

  const auto apply_outcome = manager.apply(dasall::infra::ota::UpgradePlan{});
  assert_true(apply_outcome.is_failure(),
              "IOTAManager apply should fail observably when plan preconditions are missing");
  assert_true(apply_outcome.references_only_contract_result_code(),
              "IOTAManager apply failures should stay inside the contracts ResultCode space");

  const auto rollback_outcome = manager.rollback(dasall::infra::ota::RollbackToken{});
  assert_true(rollback_outcome.is_failure(),
              "IOTAManager rollback should reject an invalid token instead of mutating status implicitly");
}

}  // namespace

int main() {
  try {
    test_ota_manager_interface_accepts_precheck_apply_rollback_and_query();
    test_ota_manager_interface_reports_invalid_inputs_with_contract_shaped_failures();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}