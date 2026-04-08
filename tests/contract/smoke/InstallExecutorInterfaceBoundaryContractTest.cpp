#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

#include "ota/IInstallExecutor.h"
#include "support/TestAssertions.h"

namespace {

void test_install_executor_interface_keeps_private_install_payloads_and_contract_failures() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::ota::BootSwitchResult;
  using dasall::infra::ota::IInstallExecutor;
  using dasall::infra::ota::RollbackResult;
  using dasall::infra::ota::StageArtifactResult;

  static_assert(std::is_same_v<decltype(std::declval<IInstallExecutor&>().stage_artifact(
                                   std::declval<const dasall::infra::ota::ArtifactDescriptor&>(),
                                   std::declval<std::string_view>())),
                               StageArtifactResult>);
  static_assert(std::is_same_v<decltype(std::declval<IInstallExecutor&>().activate_plan(
                                   std::declval<const dasall::infra::ota::SlotPlan&>())),
                               BootSwitchResult>);
  static_assert(std::is_same_v<decltype(std::declval<IInstallExecutor&>().revert_install(
                                   std::declval<const dasall::infra::ota::RollbackToken&>())),
                               RollbackResult>);
  static_assert(std::is_same_v<decltype(StageArtifactResult{}.error), std::optional<ErrorInfo>>);
  static_assert(std::is_same_v<decltype(StageArtifactResult{}.result_code), ResultCode>);
  static_assert(std::is_same_v<decltype(BootSwitchResult{}.error), std::optional<ErrorInfo>>);
  static_assert(std::is_same_v<decltype(RollbackResult{}.error), std::optional<ErrorInfo>>);
}

void test_install_executor_boundary_results_keep_install_evidence_and_failure_mapping_stable() {
  using dasall::contracts::ResultCode;
  using dasall::tests::support::assert_true;

  const auto stage_success = dasall::infra::ota::StageArtifactResult::success(
      dasall::infra::ota::InstallEvidence{
          .artifact_id = std::string("artifact-rootfs-a"),
          .written_target = std::string("/dev/mmcblk0p3"),
          .checksum = std::string("sha256:artifact-004"),
          .install_ts = std::string("2026-04-01T12:05:00Z"),
          .installer_version = std::string("install-executor/1.0"),
      });
  assert_true(stage_success.staged && stage_success.references_only_contract_error_types(),
              "stage_artifact success should keep InstallEvidence as an infra-private replay payload");

  const auto switch_failure = dasall::infra::ota::BootSwitchResult::failure(
      ResultCode::ValidationFieldMissing,
      std::string("inactive slot unavailable"),
      std::string("ota.activate_plan"),
      std::string("BoundaryExecutor"));
  assert_true(switch_failure.references_only_contract_error_types(),
              "boot switch failures should remain observable through contracts ResultCode/ErrorInfo only");

  const auto rollback_failure = dasall::infra::ota::RollbackResult::failure(
      ResultCode::ValidationFieldMissing,
      std::string("rollback token expired"),
      std::string("ota.revert_install"),
      std::string("BoundaryExecutor"));
  assert_true(rollback_failure.references_only_contract_error_types(),
              "rollback failures should remain observable through contracts ResultCode/ErrorInfo only");
}

}  // namespace

int main() {
  try {
    test_install_executor_interface_keeps_private_install_payloads_and_contract_failures();
    test_install_executor_boundary_results_keep_install_evidence_and_failure_mapping_stable();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}