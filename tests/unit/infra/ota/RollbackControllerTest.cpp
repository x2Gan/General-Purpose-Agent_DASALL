#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ota/RollbackController.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

using dasall::infra::ota::BootMutationResult;
using dasall::infra::ota::BootTargetQueryResult;
using dasall::infra::ota::IBootControlAdapter;
using dasall::infra::ota::InstallEvidence;
using dasall::infra::ota::IRepoPointerRecoveryAdapter;
using dasall::infra::ota::IRollbackEvidenceWriter;
using dasall::infra::ota::ITimeProvider;
using dasall::infra::ota::RepoPointerRecoveryResult;
using dasall::infra::ota::RollbackController;
using dasall::infra::ota::RollbackEvidenceResult;
using dasall::infra::ota::RollbackToken;

RollbackToken make_rollback_token() {
  return RollbackToken{
      .rollback_id = std::string("rollback-012"),
      .previous_boot_target = std::string("rootfs_a"),
      .staged_artifacts = {std::string("artifact-rootfs-012"),
                           std::string("artifact-config-012")},
      .created_at = std::string("2026-04-07T13:10:00Z"),
      .expires_at = std::string("2026-04-07T13:25:00Z"),
  };
}

std::vector<InstallEvidence> make_install_evidence() {
  return {
      InstallEvidence{
          .artifact_id = std::string("artifact-rootfs-012"),
          .written_target = std::string("/dev/mmcblk0p3"),
          .checksum = std::string("sha256:rootfs-012"),
          .install_ts = std::string("2026-04-07T13:05:00Z"),
          .installer_version = std::string("install-executor/1.0"),
      },
      InstallEvidence{
          .artifact_id = std::string("artifact-config-012"),
          .written_target = std::string("repo://staging/config/current"),
          .checksum = std::string("sha256:config-012"),
          .install_ts = std::string("2026-04-07T13:06:00Z"),
          .installer_version = std::string("install-executor/1.0"),
      },
  };
}

class FakeBootControlAdapter final : public IBootControlAdapter {
 public:
  explicit FakeBootControlAdapter(std::string active_target)
      : active_target_(std::move(active_target)) {}

  [[nodiscard]] BootTargetQueryResult get_active_target() const override {
    return BootTargetQueryResult::success(active_target_);
  }

  [[nodiscard]] BootMutationResult set_next_boot(std::string_view target) override {
    if (return_failure_) {
      return BootMutationResult::failure(
          dasall::contracts::ResultCode::RuntimeRetryExhausted,
          "boot restore mutation failed",
          "ota.rollback.restore_boot_target",
          "FakeBootControlAdapter");
    }

    ++set_calls;
    next_target_ = std::string(target);
    return BootMutationResult::success(next_target_, std::string("set_next_boot"));
  }

  [[nodiscard]] BootMutationResult mark_boot_success(std::string_view target) override {
    return BootMutationResult::success(std::string(target),
                                       std::string("mark_boot_success"));
  }

  [[nodiscard]] BootMutationResult mark_boot_failed(std::string_view target) override {
    return BootMutationResult::success(std::string(target),
                                       std::string("mark_boot_failed"));
  }

  void set_failure(bool value) { return_failure_ = value; }
  [[nodiscard]] const std::string& next_target() const { return next_target_; }
  [[nodiscard]] std::size_t mutation_calls() const { return set_calls; }

 private:
  std::string active_target_;
  std::string next_target_;
  bool return_failure_ = false;
  std::size_t set_calls = 0;
};

class FakeRepoPointerRecoveryAdapter final : public IRepoPointerRecoveryAdapter {
 public:
  RepoPointerRecoveryResult result;
  mutable std::size_t calls = 0;

  [[nodiscard]] RepoPointerRecoveryResult recover(
      const RollbackToken&,
      const std::vector<InstallEvidence>&) const override {
    ++calls;
    return result;
  }
};

class FakeRollbackEvidenceWriter final : public IRollbackEvidenceWriter {
 public:
  RollbackEvidenceResult result;
  mutable std::size_t calls = 0;

  [[nodiscard]] RollbackEvidenceResult write(
      const RollbackToken&,
      std::string_view,
      const std::vector<std::string>&) const override {
    ++calls;
    return result;
  }
};

class FakeTimeProvider final : public ITimeProvider {
 public:
  explicit FakeTimeProvider(std::string now) : now_(std::move(now)) {}

  [[nodiscard]] std::string now_utc() const override { return now_; }

 private:
  std::string now_;
};

void test_rollback_controller_restores_boot_target_recovers_repo_pointer_and_records_evidence() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  FakeBootControlAdapter boot_control_adapter("rootfs_b");
  FakeRepoPointerRecoveryAdapter repo_recovery_adapter;
  repo_recovery_adapter.result = RepoPointerRecoveryResult::success(
      {std::string("repo://current/config")});
  FakeRollbackEvidenceWriter evidence_writer;
  evidence_writer.result = RollbackEvidenceResult::success(
      std::string("audit://ota/rollback/012"));
  FakeTimeProvider time_provider("2026-04-07T13:15:00Z");

  RollbackController controller(RollbackController::Dependencies{
      .boot_control_adapter = &boot_control_adapter,
      .repo_pointer_recovery_adapter = &repo_recovery_adapter,
      .evidence_writer = &evidence_writer,
      .time_provider = &time_provider,
  });

  const auto rollback_result = controller.rollback(make_rollback_token(), make_install_evidence());

  assert_true(rollback_result.rolled_back && rollback_result.references_only_contract_error_types(),
              "RollbackController should restore the previous boot target, recover repo pointers, and emit a durable evidence ref on success");
  assert_equal(std::string("rootfs_a"), rollback_result.restored_target,
               "rollback should restore the previous boot target recorded in the rollback token");
  assert_equal(std::string("rootfs_a"), boot_control_adapter.next_target(),
               "restore_boot_target should drive the boot control adapter back to the previous target");
  assert_equal(static_cast<std::size_t>(1), repo_recovery_adapter.calls,
               "rollback should recover repo pointers exactly once on the success path");
  assert_equal(static_cast<std::size_t>(1), evidence_writer.calls,
               "rollback should record evidence exactly once after recovery succeeds");
}

void test_rollback_controller_exposes_restore_and_repo_recovery_helpers() {
  using dasall::tests::support::assert_true;

  FakeBootControlAdapter boot_control_adapter("rootfs_b");
  FakeRepoPointerRecoveryAdapter repo_recovery_adapter;
  repo_recovery_adapter.result = RepoPointerRecoveryResult::success(
      {std::string("repo://current/config")});
  FakeRollbackEvidenceWriter evidence_writer;
  evidence_writer.result = RollbackEvidenceResult::success(
      std::string("audit://ota/rollback/012"));
  FakeTimeProvider time_provider("2026-04-07T13:15:00Z");

  RollbackController controller(RollbackController::Dependencies{
      .boot_control_adapter = &boot_control_adapter,
      .repo_pointer_recovery_adapter = &repo_recovery_adapter,
      .evidence_writer = &evidence_writer,
      .time_provider = &time_provider,
  });

  const auto boot_restore = controller.restore_boot_target(std::string_view("rootfs_a"));
  assert_true(boot_restore.applied && boot_restore.references_only_contract_error_types(),
              "RollbackController should expose restore_boot_target as a contract-shaped boot mutation boundary");

  const auto repo_restore = controller.recover_repo_pointer(make_rollback_token(), make_install_evidence());
  assert_true(repo_restore.recovered && repo_restore.references_only_contract_error_types(),
              "RollbackController should expose recover_repo_pointer as a contract-shaped recovery boundary for repo_bound artifacts");
}

void test_rollback_controller_rejects_expired_tokens_and_surfaces_failures() {
  using dasall::tests::support::assert_true;

  FakeBootControlAdapter boot_control_adapter("rootfs_b");
  FakeRepoPointerRecoveryAdapter repo_recovery_adapter;
  repo_recovery_adapter.result = RepoPointerRecoveryResult::failure(
      dasall::contracts::ResultCode::RuntimeRetryExhausted,
      std::string("repo pointer recovery failed"),
      std::string("ota.rollback.recover_repo_pointer"),
      std::string("FakeRepoPointerRecoveryAdapter"));
  FakeRollbackEvidenceWriter evidence_writer;
  evidence_writer.result = RollbackEvidenceResult::success(
      std::string("audit://ota/rollback/012"));

    FakeTimeProvider expired_time_provider("2026-04-07T13:30:00Z");
    RollbackController expired_controller(RollbackController::Dependencies{
      .boot_control_adapter = &boot_control_adapter,
      .repo_pointer_recovery_adapter = &repo_recovery_adapter,
      .evidence_writer = &evidence_writer,
      .time_provider = &expired_time_provider,
  });
  const auto expired_result = expired_controller.rollback(make_rollback_token(), make_install_evidence());
  assert_true(!expired_result.rolled_back && expired_result.references_only_contract_error_types(),
              "RollbackController should reject expired rollback tokens before any recovery side effects occur");

  FakeTimeProvider time_provider("2026-04-07T13:15:00Z");
  RollbackController failing_controller(RollbackController::Dependencies{
      .boot_control_adapter = &boot_control_adapter,
      .repo_pointer_recovery_adapter = &repo_recovery_adapter,
      .evidence_writer = &evidence_writer,
      .time_provider = &time_provider,
  });
  const auto failing_result = failing_controller.rollback(make_rollback_token(), make_install_evidence());
  assert_true(!failing_result.rolled_back && failing_result.references_only_contract_error_types(),
              "RollbackController should surface repo recovery failures as independently observable rollback failures");
}

}  // namespace

int main() {
  try {
    test_rollback_controller_restores_boot_target_recovers_repo_pointer_and_records_evidence();
    test_rollback_controller_exposes_restore_and_repo_recovery_helpers();
    test_rollback_controller_rejects_expired_tokens_and_surfaces_failures();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}