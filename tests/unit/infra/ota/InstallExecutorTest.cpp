#include <exception>
#include <iostream>
#include <string>
#include <string_view>

#include "ota/InstallExecutor.h"
#include "support/TestAssertions.h"

namespace {

using dasall::infra::ota::ArtifactClass;
using dasall::infra::ota::ArtifactDescriptor;
using dasall::infra::ota::ArtifactWriteResult;
using dasall::infra::ota::BootSwitchResult;
using dasall::infra::ota::CleanupResult;
using dasall::infra::ota::IArtifactWriter;
using dasall::infra::ota::IInstallCleanupHandler;
using dasall::infra::ota::IInstallRevertAdapter;
using dasall::infra::ota::IPlanActivationAdapter;
using dasall::infra::ota::InstallExecutor;
using dasall::infra::ota::RollbackResult;
using dasall::infra::ota::RollbackToken;
using dasall::infra::ota::SlotPlan;

ArtifactDescriptor make_repo_artifact_descriptor() {
  return ArtifactDescriptor{
      .artifact_id = std::string("artifact-config-009"),
      .artifact_class = ArtifactClass::RepoBound,
      .target_slot_group = std::string("config-repo"),
      .version = std::string("2026.04.07"),
      .hardware_selector = {std::string("board-a")},
      .dependency_refs = {},
  };
}

ArtifactDescriptor make_slot_artifact_descriptor() {
  return ArtifactDescriptor{
      .artifact_id = std::string("artifact-rootfs-009"),
      .artifact_class = ArtifactClass::SlotBound,
      .target_slot_group = std::string("rootfs"),
      .version = std::string("1.2.9"),
      .hardware_selector = {std::string("board-a")},
      .dependency_refs = {},
  };
}

SlotPlan make_slot_plan() {
  return SlotPlan{
      .active_slot = std::string("rootfs_a"),
      .target_slot = std::string("rootfs_b"),
      .slot_group = std::string("rootfs"),
      .switch_policy = std::string("confirm_after_boot"),
      .confirm_deadline = std::string("2026-04-08T12:00:00Z"),
  };
}

RollbackToken make_rollback_token() {
  return RollbackToken{
      .rollback_id = std::string("rollback-009"),
      .previous_boot_target = std::string("rootfs_a"),
      .staged_artifacts = {std::string("artifact-rootfs-009")},
      .created_at = std::string("2026-04-07T12:00:00Z"),
      .expires_at = std::string("2026-04-08T12:00:00Z"),
  };
}

class FakeArtifactWriter final : public IArtifactWriter {
 public:
  ArtifactWriteResult repo_result;
  ArtifactWriteResult slot_result;
  mutable std::size_t repo_calls = 0;
  mutable std::size_t slot_calls = 0;
  mutable std::string last_repo_target;
  mutable std::string last_slot_target;

  [[nodiscard]] ArtifactWriteResult write_repo_bound(
      const ArtifactDescriptor&,
      std::string_view target) const override {
    ++repo_calls;
    last_repo_target = std::string(target);
    return repo_result;
  }

  [[nodiscard]] ArtifactWriteResult write_slot_bound(
      const ArtifactDescriptor&,
      std::string_view target) const override {
    ++slot_calls;
    last_slot_target = std::string(target);
    return slot_result;
  }
};

class FakeCleanupHandler final : public IInstallCleanupHandler {
 public:
  CleanupResult result;
  mutable std::size_t calls = 0;
  mutable std::string last_target;

  [[nodiscard]] CleanupResult cleanup_failed_stage(
      const ArtifactDescriptor&,
      std::string_view target) const override {
    ++calls;
    last_target = std::string(target);
    return result;
  }
};

class FakeActivationAdapter final : public IPlanActivationAdapter {
 public:
  BootSwitchResult result;
  mutable std::size_t calls = 0;

  [[nodiscard]] BootSwitchResult activate(const SlotPlan&) const override {
    ++calls;
    return result;
  }
};

class FakeRevertAdapter final : public IInstallRevertAdapter {
 public:
  RollbackResult result;
  mutable std::size_t calls = 0;

  [[nodiscard]] RollbackResult revert(const RollbackToken&) const override {
    ++calls;
    return result;
  }
};

void test_install_executor_routes_repo_bound_and_slot_bound_artifacts_to_distinct_writers() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  FakeArtifactWriter writer;
  writer.repo_result = ArtifactWriteResult{
      .written = true,
      .written_target = std::string("repo://staging/config/current"),
      .checksum = std::string("sha256:repo-009"),
      .install_ts = std::string("2026-04-07T12:05:00Z"),
      .installer_version = std::string("install-executor/1.0"),
      .detail = std::string("repo staged"),
  };
  writer.slot_result = ArtifactWriteResult{
      .written = true,
      .written_target = std::string("/dev/mmcblk0p3"),
      .checksum = std::string("sha256:slot-009"),
      .install_ts = std::string("2026-04-07T12:06:00Z"),
      .installer_version = std::string("install-executor/1.0"),
      .detail = std::string("slot staged"),
  };
  FakeCleanupHandler cleanup_handler;

  InstallExecutor executor(InstallExecutor::Dependencies{
      .artifact_writer = &writer,
      .cleanup_handler = &cleanup_handler,
      .activation_adapter = nullptr,
      .revert_adapter = nullptr,
  });

  const auto repo_result = executor.stage_artifact(
      make_repo_artifact_descriptor(),
      std::string_view("repo://staging/config/current"));
  const auto slot_result = executor.stage_artifact(
      make_slot_artifact_descriptor(),
      std::string_view("/dev/mmcblk0p3"));

  assert_true(repo_result.staged && repo_result.evidence.is_valid(),
              "InstallExecutor should emit InstallEvidence for repo_bound artifacts after staging into the repo target");
  assert_true(slot_result.staged && slot_result.evidence.is_valid(),
              "InstallExecutor should emit InstallEvidence for slot_bound artifacts after staging into the inactive slot target");
  assert_equal(static_cast<std::size_t>(1), writer.repo_calls,
               "InstallExecutor should route repo_bound artifacts through the repo writer branch exactly once");
  assert_equal(static_cast<std::size_t>(1), writer.slot_calls,
               "InstallExecutor should route slot_bound artifacts through the slot writer branch exactly once");
}

void test_install_executor_cleans_partial_state_when_materialization_fails() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  FakeArtifactWriter writer;
  writer.slot_result = ArtifactWriteResult{
      .written = false,
      .written_target = {},
      .checksum = {},
      .install_ts = {},
      .installer_version = {},
      .detail = std::string("slot writer returned EIO"),
  };
  FakeCleanupHandler cleanup_handler;
  cleanup_handler.result = CleanupResult{
      .cleaned = true,
      .detail = std::string("partial slot payload removed"),
  };

  InstallExecutor executor(InstallExecutor::Dependencies{
      .artifact_writer = &writer,
      .cleanup_handler = &cleanup_handler,
      .activation_adapter = nullptr,
      .revert_adapter = nullptr,
  });

  const auto failed_stage = executor.stage_artifact(
      make_slot_artifact_descriptor(),
      std::string_view("/dev/mmcblk0p3"));

  assert_true(!failed_stage.staged && failed_stage.references_only_contract_error_types(),
              "InstallExecutor should surface slot write failures as contract-shaped errors after the cleanup path runs");
  assert_equal(static_cast<std::size_t>(1), cleanup_handler.calls,
               "InstallExecutor should invoke cleanup exactly once after a failed materialization attempt");
}

void test_install_executor_delegates_activation_and_revert_results() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  FakeArtifactWriter writer;
  FakeCleanupHandler cleanup_handler;
  FakeActivationAdapter activation_adapter;
  activation_adapter.result = BootSwitchResult::success(std::string("rootfs_b"), true);
  FakeRevertAdapter revert_adapter;
  revert_adapter.result = RollbackResult::success(
      std::string("rootfs_a"),
      std::string("audit://ota/rollback/009"));

  InstallExecutor executor(InstallExecutor::Dependencies{
      .artifact_writer = &writer,
      .cleanup_handler = &cleanup_handler,
      .activation_adapter = &activation_adapter,
      .revert_adapter = &revert_adapter,
  });

  const auto activation_result = executor.activate_plan(make_slot_plan());
  const auto revert_result = executor.revert_install(make_rollback_token());

  assert_true(activation_result.switched && activation_result.references_only_contract_error_types(),
              "InstallExecutor should keep activation inside the BootSwitchResult boundary while handing off to a later slot-switch implementation");
  assert_true(revert_result.rolled_back && revert_result.references_only_contract_error_types(),
              "InstallExecutor should keep install revert inside the RollbackResult boundary while handing off to a later rollback implementation");
  assert_equal(static_cast<std::size_t>(1), activation_adapter.calls,
               "InstallExecutor should delegate activation exactly once for a valid slot plan");
  assert_equal(static_cast<std::size_t>(1), revert_adapter.calls,
               "InstallExecutor should delegate revert exactly once for a valid rollback token");
}

}  // namespace

int main() {
  try {
    test_install_executor_routes_repo_bound_and_slot_bound_artifacts_to_distinct_writers();
    test_install_executor_cleans_partial_state_when_materialization_fails();
    test_install_executor_delegates_activation_and_revert_results();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}