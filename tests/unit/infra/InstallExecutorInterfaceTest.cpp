#include <exception>
#include <iostream>
#include <string>

#include "ota/IInstallExecutor.h"
#include "support/TestAssertions.h"

namespace {

dasall::infra::ota::ArtifactDescriptor make_valid_artifact_descriptor() {
  return dasall::infra::ota::ArtifactDescriptor{
      .artifact_id = std::string("artifact-rootfs-a"),
      .artifact_class = dasall::infra::ota::ArtifactClass::SlotBound,
      .target_slot_group = std::string("rootfs"),
      .version = std::string("1.2.3"),
      .hardware_selector = {std::string("board-a")},
      .dependency_refs = {},
  };
}

dasall::infra::ota::SlotPlan make_valid_slot_plan() {
  return dasall::infra::ota::SlotPlan{
      .active_slot = std::string("rootfs_a"),
      .target_slot = std::string("rootfs_b"),
      .slot_group = std::string("rootfs"),
      .switch_policy = std::string("confirm_after_boot"),
      .confirm_deadline = std::string("2026-04-02T12:00:00Z"),
  };
}

dasall::infra::ota::RollbackToken make_valid_rollback_token() {
  return dasall::infra::ota::RollbackToken{
      .rollback_id = std::string("rollback-004"),
      .previous_boot_target = std::string("rootfs_a"),
      .staged_artifacts = {std::string("artifact-rootfs-a")},
      .created_at = std::string("2026-04-01T12:00:00Z"),
      .expires_at = std::string("2026-04-02T12:00:00Z"),
  };
}

class NullInstallExecutor final : public dasall::infra::ota::IInstallExecutor {
 public:
  dasall::infra::ota::StageArtifactResult stage_artifact(
      const dasall::infra::ota::ArtifactDescriptor& artifact_descriptor,
      std::string_view target) override {
    if (!artifact_descriptor.is_valid() || target.empty()) {
      return dasall::infra::ota::StageArtifactResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "artifact descriptor and target must stay explicit for staging",
          "ota.stage_artifact",
          "NullInstallExecutor");
    }

    return dasall::infra::ota::StageArtifactResult::success(
        dasall::infra::ota::InstallEvidence{
            .artifact_id = artifact_descriptor.artifact_id,
            .written_target = std::string(target),
            .checksum = std::string("sha256:artifact-004"),
            .install_ts = std::string("2026-04-01T12:05:00Z"),
            .installer_version = std::string("install-executor/1.0"),
        });
  }

  dasall::infra::ota::BootSwitchResult activate_plan(
      const dasall::infra::ota::SlotPlan& slot_plan) override {
    if (!slot_plan.is_valid()) {
      return dasall::infra::ota::BootSwitchResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "slot plan must target an inactive slot and keep confirm deadline explicit",
          "ota.activate_plan",
          "NullInstallExecutor");
    }

    return dasall::infra::ota::BootSwitchResult::success(slot_plan.target_slot, true);
  }

  dasall::infra::ota::RollbackResult revert_install(
      const dasall::infra::ota::RollbackToken& rollback_token) override {
    if (!rollback_token.is_valid()) {
      return dasall::infra::ota::RollbackResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "rollback token must remain attributable and non-empty",
          "ota.revert_install",
          "NullInstallExecutor");
    }

    return dasall::infra::ota::RollbackResult::success(
        rollback_token.previous_boot_target,
        std::string("audit://ota/rollback/004"));
  }
};

void test_install_executor_interface_binds_stage_activate_and_revert_outputs() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  NullInstallExecutor executor;

  const auto stage_result = executor.stage_artifact(
      make_valid_artifact_descriptor(),
      std::string_view("/dev/mmcblk0p3"));
  assert_true(stage_result.staged && stage_result.evidence.is_valid(),
              "IInstallExecutor should bind stage_artifact to InstallEvidence so writes stay replayable and auditable");
  assert_equal(std::string("artifact-rootfs-a"), stage_result.evidence.artifact_id,
               "stage_artifact should preserve the frozen artifact_id into InstallEvidence");

  const auto switch_result = executor.activate_plan(make_valid_slot_plan());
  assert_true(switch_result.switched && switch_result.references_only_contract_error_types(),
              "IInstallExecutor should activate a valid slot plan through a dedicated boot-switch result boundary");

  const auto rollback_result = executor.revert_install(make_valid_rollback_token());
  assert_true(rollback_result.rolled_back && rollback_result.references_only_contract_error_types(),
              "IInstallExecutor should expose rollback completion without leaking bootloader internals into contracts");
}

void test_install_executor_interface_reports_invalid_inputs_observably() {
  using dasall::tests::support::assert_true;

  NullInstallExecutor executor;

  const auto stage_failure = executor.stage_artifact(dasall::infra::ota::ArtifactDescriptor{}, std::string_view{});
  assert_true(!stage_failure.staged && stage_failure.references_only_contract_error_types(),
              "IInstallExecutor should reject unspecified stage inputs with contract-shaped failures");

  const auto switch_failure = executor.activate_plan(dasall::infra::ota::SlotPlan{});
  assert_true(!switch_failure.switched && switch_failure.references_only_contract_error_types(),
              "IInstallExecutor should reject invalid slot plans before any boot switch can happen");

  const auto rollback_failure = executor.revert_install(dasall::infra::ota::RollbackToken{});
  assert_true(!rollback_failure.rolled_back && rollback_failure.references_only_contract_error_types(),
              "IInstallExecutor should reject invalid rollback tokens rather than synthesizing a rollback success");
}

}  // namespace

int main() {
  try {
    test_install_executor_interface_binds_stage_activate_and_revert_outputs();
    test_install_executor_interface_reports_invalid_inputs_observably();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}