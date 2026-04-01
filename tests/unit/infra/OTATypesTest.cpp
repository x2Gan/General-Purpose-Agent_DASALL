#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "ota/OTATypes.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::contracts::ErrorInfo make_precheck_error(int code, std::string message) {
  return dasall::contracts::ErrorInfo{
      .failure_type = dasall::contracts::ResultCodeCategory::Validation,
      .retryable = false,
      .safe_to_replan = false,
      .details = dasall::contracts::ErrorDetails{
          .code = code,
          .message = std::move(message),
          .stage = std::string("ota.precheck"),
      },
      .source_ref = dasall::contracts::ErrorSourceRefMinimal{
          .ref_type = std::string("infra.ota"),
          .ref_id = std::string("OTAPrecheckService"),
      },
  };
}

void test_ota_types_freeze_core_fields_and_success_paths() {
  using dasall::contracts::ResultCode;
  using dasall::infra::ota::ArtifactClass;
  using dasall::infra::ota::ArtifactDescriptor;
  using dasall::infra::ota::InstallEvidence;
  using dasall::infra::ota::OTAStatusSnapshot;
  using dasall::infra::ota::PackageDescriptor;
  using dasall::infra::ota::PrecheckReport;
  using dasall::infra::ota::RollbackToken;
  using dasall::infra::ota::SlotPlan;
  using dasall::infra::ota::UpgradeOutcome;
  using dasall::infra::ota::UpgradePlan;
  using dasall::infra::ota::UpgradeRequester;
  using dasall::infra::ota::VerifiedPackageManifest;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(UpgradePlan{}.plan_id), std::string>);
  static_assert(std::is_same_v<decltype(UpgradeRequester{}.request_id), std::string>);
  static_assert(std::is_same_v<decltype(PackageDescriptor{}.size_bytes), std::uint64_t>);
  static_assert(std::is_same_v<decltype(ArtifactDescriptor{}.artifact_class), ArtifactClass>);
  static_assert(std::is_same_v<decltype(UpgradeOutcome{}.result_code),
                               std::optional<ResultCode>>);

  const UpgradePlan plan{
      .plan_id = std::string("ota-plan-001"),
      .requested_by = UpgradeRequester{
          .actor_ref = std::string("ops-user"),
          .request_id = std::string("req-ota-001"),
      },
      .target_scope = std::string("device.local"),
      .artifact_refs = {std::string("artifact-rootfs-a"), std::string("artifact-repo-policy")},
      .strategy = std::string("safe_switch"),
      .validate_only = false,
  };
  assert_true(plan.is_valid(),
              "upgrade plan should require stable plan id, requester, target scope, artifact refs, and strategy");

  const PackageDescriptor package_descriptor{
      .package_id = std::string("pkg-ota-001"),
      .package_uri = std::string("https://updates.example.invalid/ota/pkg-ota-001.raucb"),
      .manifest_version = std::string("1"),
      .package_kind = std::string("bundle"),
      .signed_metadata_ref = std::string("targets/v1/pkg-ota-001.json"),
      .size_bytes = 4096,
  };
  assert_true(package_descriptor.is_valid(),
              "package descriptor should keep package origin, manifest version, kind, metadata ref, and size frozen");

  const ArtifactDescriptor artifact_descriptor{
      .artifact_id = std::string("artifact-rootfs-a"),
      .artifact_class = ArtifactClass::SlotBound,
      .target_slot_group = std::string("rootfs"),
      .version = std::string("1.2.3"),
      .hardware_selector = {std::string("board-a"), std::string("board-b")},
      .dependency_refs = {std::string("artifact-repo-policy")},
  };
  assert_true(artifact_descriptor.is_valid(),
              "artifact descriptor should freeze the slot/repo class split and compatibility selectors");

  const VerifiedPackageManifest manifest{
      .package_id = package_descriptor.package_id,
      .signature_ok = true,
      .hash_set = {std::string("sha256:abc"), std::string("sha256:def")},
      .release_counter = 42,
      .compatible_profiles = {std::string("desktop_full"), std::string("edge_balanced")},
      .artifact_list = {artifact_descriptor},
  };
  assert_true(manifest.is_valid(),
              "verified package manifest should keep package id, hashes, release counter, compatible profiles, and artifact list frozen");

  const PrecheckReport precheck_report{
      .health_ok = true,
      .resource_ok = true,
      .compatibility_ok = true,
      .policy_ok = true,
      .blocking_reasons = {},
  };
  assert_true(precheck_report.is_valid(),
              "successful precheck should not carry blocking reasons");

  const SlotPlan slot_plan{
      .active_slot = std::string("rootfs_a"),
      .target_slot = std::string("rootfs_b"),
      .slot_group = std::string("rootfs"),
      .switch_policy = std::string("confirm_after_boot"),
      .confirm_deadline = std::string("2026-04-02T08:00:00Z"),
  };
  assert_true(slot_plan.is_valid(),
              "slot plan should target an inactive slot and keep confirm deadline explicit");

  const RollbackToken rollback_token{
      .rollback_id = std::string("rollback-001"),
      .previous_boot_target = std::string("rootfs_a"),
      .staged_artifacts = {artifact_descriptor.artifact_id},
      .created_at = std::string("2026-04-01T08:00:00Z"),
      .expires_at = std::string("2026-04-02T08:00:00Z"),
  };
  assert_true(rollback_token.is_valid(),
              "rollback token should keep previous boot target and staged artifact anchors frozen");

  const InstallEvidence install_evidence{
      .artifact_id = artifact_descriptor.artifact_id,
      .written_target = std::string("/dev/mmcblk0p3"),
      .checksum = std::string("sha256:abc"),
      .install_ts = std::string("2026-04-01T08:05:00Z"),
      .installer_version = std::string("ota-installer/1.0"),
  };
  assert_true(install_evidence.is_valid(),
              "install evidence should keep replayable target, checksum, timestamp, and installer version");

  const UpgradeOutcome upgrade_outcome{
      .phase = std::string("success"),
      .result_code = std::nullopt,
      .rollback_applied = false,
      .final_version_set = {std::string("rootfs=1.2.3"), std::string("repo_policy=2026.04")},
      .evidence_ref = std::string("audit://ota/apply/001"),
  };
  assert_true(upgrade_outcome.is_valid(),
              "successful upgrade outcome should use the frozen phase, rollback flag, version set, and evidence ref semantics");

  const OTAStatusSnapshot status_snapshot{
      .last_plan_id = plan.plan_id,
      .state = std::string("pending_confirm"),
      .active_slot = slot_plan.target_slot,
      .pending_confirm = true,
      .last_failure_code = ResultCode::ValidationFieldMissing,
      .backlog_count = 1,
  };
  assert_true(status_snapshot.is_valid(),
              "status snapshot should keep query-only state, active slot, pending confirm, and backlog count fields frozen");
  assert_equal(true, status_snapshot.pending_confirm,
               "pending_confirm should stay as a dedicated boolean instead of leaking bootloader internals");
}

void test_ota_types_reject_duplicate_or_inconsistent_failure_inputs() {
  using dasall::contracts::ResultCode;
  using dasall::infra::ota::ArtifactClass;
  using dasall::infra::ota::ArtifactDescriptor;
  using dasall::infra::ota::OTAStatusSnapshot;
  using dasall::infra::ota::PrecheckReport;
  using dasall::infra::ota::SlotPlan;
  using dasall::infra::ota::UpgradeOutcome;
  using dasall::tests::support::assert_true;

  const ArtifactDescriptor duplicate_selector_artifact{
      .artifact_id = std::string("artifact-rootfs-a"),
      .artifact_class = ArtifactClass::SlotBound,
      .target_slot_group = std::string("rootfs"),
      .version = std::string("1.2.3"),
      .hardware_selector = {std::string("board-a"), std::string("board-a")},
      .dependency_refs = {},
  };
  assert_true(!duplicate_selector_artifact.is_valid(),
              "artifact descriptor should reject duplicate hardware selectors to keep compatibility matching deterministic");

  const PrecheckReport inconsistent_precheck{
      .health_ok = true,
      .resource_ok = true,
      .compatibility_ok = true,
      .policy_ok = true,
      .blocking_reasons = {make_precheck_error(1001, "disk pressure")},
  };
  assert_true(!inconsistent_precheck.is_valid(),
              "successful precheck should not carry blocking reasons");

  const PrecheckReport blocked_precheck{
      .health_ok = false,
      .resource_ok = true,
      .compatibility_ok = true,
      .policy_ok = true,
      .blocking_reasons = {make_precheck_error(1002, "health gate failed")},
  };
  assert_true(blocked_precheck.is_valid(),
              "failed precheck should carry contract-shaped blocking reasons so apply can fail without side effects");

  const SlotPlan invalid_slot_plan{
      .active_slot = std::string("rootfs_a"),
      .target_slot = std::string("rootfs_a"),
      .slot_group = std::string("rootfs"),
      .switch_policy = std::string("confirm_after_boot"),
      .confirm_deadline = std::string("2026-04-02T08:00:00Z"),
  };
  assert_true(!invalid_slot_plan.is_valid(),
              "slot plan should reject switching to the current active slot");

  const UpgradeOutcome invalid_success_outcome{
      .phase = std::string("success"),
      .result_code = ResultCode::ProviderTimeout,
      .rollback_applied = false,
      .final_version_set = {std::string("rootfs=1.2.3")},
      .evidence_ref = std::string("audit://ota/apply/001"),
  };
  assert_true(!invalid_success_outcome.is_valid(),
              "upgrade outcome should not mix a success phase payload with a failure result code");

  const OTAStatusSnapshot invalid_snapshot{
      .last_plan_id = std::string("ota-plan-001"),
      .state = std::string("degraded"),
      .active_slot = std::string("rootfs_b"),
      .pending_confirm = false,
      .last_failure_code = static_cast<ResultCode>(9000),
      .backlog_count = 3,
  };
  assert_true(!invalid_snapshot.is_valid(),
              "status snapshot should reject unknown result-code segments in the failure summary");
}

}  // namespace

int main() {
  try {
    test_ota_types_freeze_core_fields_and_success_paths();
    test_ota_types_reject_duplicate_or_inconsistent_failure_inputs();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}