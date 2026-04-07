#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "health/IHealthMonitor.h"
#include "ota/ArtifactCompatibilityEvaluator.h"
#include "ota/BootConfirmationMonitor.h"
#include "ota/InstallExecutor.h"
#include "ota/PackageVerifier.h"
#include "ota/SlotSwitchCoordinator.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

using dasall::infra::HealthListenerSubscriptionResult;
using dasall::infra::HealthMonitorRegistrationResult;
using dasall::infra::HealthProbeRegistration;
using dasall::infra::HealthSnapshot;
using dasall::infra::HealthSnapshotResult;
using dasall::infra::IHealthMonitor;
using dasall::infra::IHealthStateListener;
using dasall::infra::ota::ArtifactClass;
using dasall::infra::ota::ArtifactCompatibilityEvaluator;
using dasall::infra::ota::ArtifactCompatibilityProfile;
using dasall::infra::ota::ArtifactDescriptor;
using dasall::infra::ota::ArtifactVerificationReport;
using dasall::infra::ota::ArtifactWriteResult;
using dasall::infra::ota::BootConfirmationMonitor;
using dasall::infra::ota::BootConfirmationPolicySnapshot;
using dasall::infra::ota::BootConfirmationRequest;
using dasall::infra::ota::BootConfirmationState;
using dasall::infra::ota::BootMutationResult;
using dasall::infra::ota::BootSwitchResult;
using dasall::infra::ota::BootSuccessSignal;
using dasall::infra::ota::BootTargetQueryResult;
using dasall::infra::ota::CleanupResult;
using dasall::infra::ota::DeviceCapabilitySnapshot;
using dasall::infra::ota::HeartbeatFreshnessReport;
using dasall::infra::ota::IArtifactWriter;
using dasall::infra::ota::IBootControlAdapter;
using dasall::infra::ota::IBootSuccessSignalProvider;
using dasall::infra::ota::IHeartbeatFreshnessSource;
using dasall::infra::ota::IInstallCleanupHandler;
using dasall::infra::ota::IPackageVerifierPolicyProvider;
using dasall::infra::ota::IPlanActivationAdapter;
using dasall::infra::ota::IRollbackTokenFactory;
using dasall::infra::ota::ISignatureVerifierAdapter;
using dasall::infra::ota::ISlotInventoryProvider;
using dasall::infra::ota::ITrustAnchorProvider;
using dasall::infra::ota::IVersionReportSource;
using dasall::infra::ota::InstallEvidence;
using dasall::infra::ota::InstallExecutor;
using dasall::infra::ota::PackageDescriptor;
using dasall::infra::ota::PackageVerificationReport;
using dasall::infra::ota::PackageVerifier;
using dasall::infra::ota::PackageVerifierPolicy;
using dasall::infra::ota::RollbackResult;
using dasall::infra::ota::RollbackToken;
using dasall::infra::ota::SlotInventory;
using dasall::infra::ota::SlotPlan;
using dasall::infra::ota::SlotSwitchCoordinator;
using dasall::infra::ota::SwitchPolicySnapshot;
using dasall::infra::ota::TrustAnchorLoadResult;
using dasall::infra::ota::TrustAnchorMaterial;
using dasall::infra::ota::VersionReportSnapshot;

PackageDescriptor make_package_descriptor() {
  return PackageDescriptor{
      .package_id = std::string("ota-pkg-017"),
      .package_uri = std::string("https://updates.example.invalid/ota-pkg-017.bundle"),
      .manifest_version = std::string("1"),
      .package_kind = std::string("bundle"),
      .signed_metadata_ref = std::string("targets/v1/ota-pkg-017.json"),
      .size_bytes = 65536,
  };
}

ArtifactDescriptor make_slot_artifact() {
  return ArtifactDescriptor{
      .artifact_id = std::string("artifact-rootfs-017"),
      .artifact_class = ArtifactClass::SlotBound,
      .target_slot_group = std::string("rootfs"),
      .version = std::string("rootfs@2026.04.07"),
      .hardware_selector = {std::string("board-a")},
      .dependency_refs = {},
  };
}

ArtifactDescriptor make_repo_artifact() {
  return ArtifactDescriptor{
      .artifact_id = std::string("artifact-app-017"),
      .artifact_class = ArtifactClass::RepoBound,
      .target_slot_group = std::string("app-repo"),
      .version = std::string("app@1.2.17"),
      .hardware_selector = {std::string("board-a")},
      .dependency_refs = {},
  };
}

PackageVerificationReport make_package_report() {
  return PackageVerificationReport{
      .signature_ok = true,
      .hash_ok = true,
      .release_counter = 17,
      .hash_set = {std::string("sha256:pkg-017")},
      .compatible_profiles = {std::string("desktop_full")},
      .artifact_list = {make_slot_artifact(), make_repo_artifact()},
  };
}

class FakeTrustAnchorProvider final : public ITrustAnchorProvider {
 public:
  [[nodiscard]] TrustAnchorLoadResult load_active_anchor(
      std::string_view,
      std::string_view) const override {
    return TrustAnchorLoadResult{
        .loaded = true,
        .material = TrustAnchorMaterial{
            .anchor_id = std::string("anchor-ota-017"),
            .algorithm = std::string("ed25519"),
            .key_format = std::string("pem"),
            .public_key_ref = std::string("secret://ota/root"),
            .version_ref = std::string("anchor-v4"),
            .not_after = std::string("2027-04-07T00:00:00Z"),
        },
        .detail = std::string("loaded"),
    };
  }
};

class FakePolicyProvider final : public IPackageVerifierPolicyProvider {
 public:
  [[nodiscard]] PackageVerifierPolicy current_policy() const override {
    return PackageVerifierPolicy{
        .verify_required = true,
        .signature_algorithm = std::string("ed25519"),
        .minimum_release_counter = 10,
        .allow_downgrade = false,
    };
  }
};

class FakeSignatureVerifier final : public ISignatureVerifierAdapter {
 public:
  [[nodiscard]] PackageVerificationReport verify_package(
      const PackageDescriptor&,
      std::string_view,
      const TrustAnchorMaterial*) const override {
    return make_package_report();
  }

  [[nodiscard]] ArtifactVerificationReport verify_artifact(
      const ArtifactDescriptor& artifact_descriptor) const override {
    return ArtifactVerificationReport{
        .hash_ok = true,
        .verified_hashes = {std::string("sha256:") + artifact_descriptor.artifact_id},
    };
  }
};

class FakeArtifactWriter final : public IArtifactWriter {
 public:
  [[nodiscard]] ArtifactWriteResult write_repo_bound(
      const ArtifactDescriptor& artifact_descriptor,
      std::string_view target) const override {
    return ArtifactWriteResult{
        .written = true,
        .written_target = std::string(target),
        .checksum = std::string("sha256:") + artifact_descriptor.artifact_id,
        .install_ts = std::string("2026-04-07T19:00:10Z"),
        .installer_version = std::string("install-executor/1.0"),
        .detail = std::string("repo staged"),
    };
  }

  [[nodiscard]] ArtifactWriteResult write_slot_bound(
      const ArtifactDescriptor& artifact_descriptor,
      std::string_view target) const override {
    return ArtifactWriteResult{
        .written = true,
        .written_target = std::string(target),
        .checksum = std::string("sha256:") + artifact_descriptor.artifact_id,
        .install_ts = std::string("2026-04-07T19:00:20Z"),
        .installer_version = std::string("install-executor/1.0"),
        .detail = std::string("slot staged"),
    };
  }
};

class FakeCleanupHandler final : public IInstallCleanupHandler {
 public:
  [[nodiscard]] CleanupResult cleanup_failed_stage(
      const ArtifactDescriptor&,
      std::string_view) const override {
    return CleanupResult{
        .cleaned = true,
        .detail = std::string("cleanup not needed"),
    };
  }
};

class FakeActivationAdapter final : public IPlanActivationAdapter {
 public:
  [[nodiscard]] BootSwitchResult activate(const SlotPlan& slot_plan) const override {
    return BootSwitchResult::success(slot_plan.target_slot, true);
  }
};

class FakeBootControlAdapter final : public IBootControlAdapter {
 public:
  explicit FakeBootControlAdapter(std::string active_target)
      : active_target_(std::move(active_target)) {}

  [[nodiscard]] BootTargetQueryResult get_active_target() const override {
    return BootTargetQueryResult::success(active_target_);
  }

  [[nodiscard]] BootMutationResult set_next_boot(std::string_view target) override {
    next_boot_target_ = std::string(target);
    return BootMutationResult::success(next_boot_target_, std::string("set_next_boot"));
  }

  [[nodiscard]] BootMutationResult mark_boot_success(std::string_view target) override {
    success_targets.emplace_back(target);
    return BootMutationResult::success(std::string(target), std::string("mark_boot_success"));
  }

  [[nodiscard]] BootMutationResult mark_boot_failed(std::string_view target) override {
    failed_targets.emplace_back(target);
    return BootMutationResult::success(std::string(target), std::string("mark_boot_failed"));
  }

  void simulate_reboot_to(std::string target) { active_target_ = std::move(target); }

  [[nodiscard]] const std::string& next_boot_target() const { return next_boot_target_; }

  std::vector<std::string> success_targets;
  std::vector<std::string> failed_targets;

 private:
  std::string active_target_;
  std::string next_boot_target_;
};

class FakeSlotInventoryProvider final : public ISlotInventoryProvider {
 public:
  [[nodiscard]] SlotInventory describe_slot_group(std::string_view slot_group) const override {
    return SlotInventory{
        .slot_group = std::string(slot_group),
        .active_slot = std::string("rootfs_a"),
        .candidate_slots = {std::string("rootfs_a"), std::string("rootfs_b")},
    };
  }
};

class FakeRollbackTokenFactory final : public IRollbackTokenFactory {
 public:
  [[nodiscard]] RollbackToken make_token(
      const SlotPlan& slot_plan,
      const std::vector<InstallEvidence>& staged_evidence) const override {
    std::vector<std::string> staged_artifacts;
    staged_artifacts.reserve(staged_evidence.size());
    for (const auto& evidence : staged_evidence) {
      staged_artifacts.push_back(evidence.artifact_id);
    }

    return RollbackToken{
        .rollback_id = std::string("rollback-017"),
        .previous_boot_target = slot_plan.active_slot,
        .staged_artifacts = std::move(staged_artifacts),
        .created_at = std::string("2026-04-07T19:00:00Z"),
        .expires_at = std::string("2026-04-07T19:30:00Z"),
    };
  }
};

class FakeHealthMonitor final : public IHealthMonitor {
 public:
  HealthSnapshotResult snapshot_result = HealthSnapshotResult::success(HealthSnapshot{
      .liveness = true,
      .readiness = true,
      .degraded = false,
      .failed_components = {},
      .version = 17,
      .timestamp = 1712516400000,
  });

  HealthMonitorRegistrationResult register_probe(
      const HealthProbeRegistration&) override {
    return HealthMonitorRegistrationResult::success();
  }

  HealthSnapshotResult evaluate_now() override { return snapshot_result; }

  [[nodiscard]] HealthSnapshotResult get_snapshot() const override {
    return snapshot_result;
  }

  HealthListenerSubscriptionResult subscribe(IHealthStateListener&) override {
    return HealthListenerSubscriptionResult::success();
  }
};

class FakeSuccessSignalProvider final : public IBootSuccessSignalProvider {
 public:
  [[nodiscard]] BootSuccessSignal read_success_signal(
      std::string_view,
      std::string_view) const override {
    return BootSuccessSignal{
        .signal_received = true,
        .self_check_ok = true,
        .detail_ref = std::string("signal://ota/confirm/success"),
        .observed_ts = 1712516401000,
    };
  }
};

class FakeHeartbeatFreshnessSource final : public IHeartbeatFreshnessSource {
 public:
  [[nodiscard]] HeartbeatFreshnessReport evaluate_freshness(
      const std::vector<std::string>&) const override {
    return HeartbeatFreshnessReport{
        .all_fresh = true,
        .watchdog_reset_detected = false,
        .stale_entities = {},
        .detail_ref = std::string("watchdog://ota/confirm/fresh"),
    };
  }
};

class FakeVersionReportSource final : public IVersionReportSource {
 public:
  [[nodiscard]] VersionReportSnapshot read_version_report(
      std::string_view) const override {
    return VersionReportSnapshot{
        .package_id = std::string("ota-pkg-017"),
        .slot_bound_versions = {std::string("rootfs@2026.04.07"), std::string("app@1.2.17")},
        .detail_ref = std::string("version://ota/confirm/match"),
        .version = 17,
        .observed_ts = 1712516402000,
    };
  }
};

void test_ota_workflow_integration_runs_apply_switch_confirm_success_through_ota_components() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  FakeTrustAnchorProvider trust_anchor_provider;
  FakePolicyProvider policy_provider;
  FakeSignatureVerifier signature_verifier;
  const PackageVerifier verifier(PackageVerifier::Dependencies{
      .trust_anchor_provider = &trust_anchor_provider,
      .policy_provider = &policy_provider,
      .signature_verifier = &signature_verifier,
  });

  const auto package_result = verifier.verify_package(make_package_descriptor());
  assert_true(package_result.verified && package_result.references_only_contract_error_types(),
              "OTAWorkflowTest should start from a contract-shaped verified package manifest before entering apply flow");

  ArtifactCompatibilityEvaluator compatibility_evaluator;
  const auto compatibility_result = compatibility_evaluator.evaluate(
      package_result.manifest,
      DeviceCapabilitySnapshot{
          .supported_hardware = {std::string("board-a")},
          .available_dependency_refs = {},
      },
      ArtifactCompatibilityProfile{
          .profile_name = std::string("desktop_full"),
          .slot_bound_allowed = true,
          .repo_bound_allowed = true,
          .forbidden_dependency_refs = {},
      });
  assert_true(compatibility_result.compatible && compatibility_result.is_valid(),
              "OTAWorkflowTest should keep the apply path open when verified artifacts match the active capability snapshot and profile");

  FakeArtifactWriter artifact_writer;
  FakeCleanupHandler cleanup_handler;
  FakeActivationAdapter activation_adapter;
  InstallExecutor install_executor(InstallExecutor::Dependencies{
      .artifact_writer = &artifact_writer,
      .cleanup_handler = &cleanup_handler,
      .activation_adapter = &activation_adapter,
      .revert_adapter = nullptr,
  });

  std::vector<InstallEvidence> staged_evidence;
  staged_evidence.reserve(package_result.manifest.artifact_list.size());
  for (const auto& artifact : package_result.manifest.artifact_list) {
    const auto stage_result = install_executor.stage_artifact(
        artifact,
        artifact.artifact_class == ArtifactClass::SlotBound
            ? std::string_view("/dev/mmcblk0p3")
            : std::string_view("repo://staging/app/current"));
    assert_true(stage_result.staged && stage_result.references_only_contract_error_types(),
                "OTAWorkflowTest should stage both slot_bound and repo_bound artifacts before slot switching");
    staged_evidence.push_back(stage_result.evidence);
  }

  FakeBootControlAdapter boot_control_adapter("rootfs_a");
  FakeSlotInventoryProvider slot_inventory_provider;
  FakeRollbackTokenFactory rollback_token_factory;
  SlotSwitchCoordinator slot_switch_coordinator(SlotSwitchCoordinator::Dependencies{
      .slot_inventory_provider = &slot_inventory_provider,
      .boot_control_adapter = &boot_control_adapter,
      .rollback_token_factory = &rollback_token_factory,
  });

  const auto preparation_result = slot_switch_coordinator.build_slot_plan(
      std::string_view("rootfs"),
      staged_evidence,
      SwitchPolicySnapshot{
          .switch_policy = std::string("confirm_after_boot"),
          .confirm_deadline = std::string("2026-04-07T19:20:00Z"),
      });
  assert_true(preparation_result.prepared && preparation_result.references_only_contract_error_types(),
              "OTAWorkflowTest should build a valid slot plan and rollback token before set_next_boot");

  const auto switch_result = slot_switch_coordinator.set_next_boot(preparation_result.slot_plan);
  assert_true(switch_result.switched && switch_result.references_only_contract_error_types(),
              "OTAWorkflowTest should switch to the inactive slot before entering boot confirmation");
  assert_equal(std::string("rootfs_b"), boot_control_adapter.next_boot_target(),
               "OTAWorkflowTest should set the inactive target as the next boot slot");

  boot_control_adapter.simulate_reboot_to(preparation_result.slot_plan.target_slot);

  FakeHealthMonitor health_monitor;
  FakeSuccessSignalProvider success_signal_provider;
  FakeHeartbeatFreshnessSource heartbeat_source;
  FakeVersionReportSource version_report_source;
  BootConfirmationMonitor boot_confirmation_monitor(BootConfirmationMonitor::Dependencies{
      .boot_control_adapter = &boot_control_adapter,
      .health_monitor = &health_monitor,
      .success_signal_provider = &success_signal_provider,
      .heartbeat_freshness_source = &heartbeat_source,
      .version_report_source = &version_report_source,
  });

  const auto confirmation_result = boot_confirmation_monitor.await_confirm(BootConfirmationRequest{
      .plan_id = std::string("ota-plan-017"),
      .package_id = package_result.manifest.package_id,
      .slot_plan = preparation_result.slot_plan,
      .rollback_token = preparation_result.rollback_token,
      .policy = BootConfirmationPolicySnapshot{
          .health_blocking_components = {std::string("boot-agent")},
          .required_heartbeat_entities = {std::string("boot-agent"), std::string("ota-agent")},
          .expected_slot_versions = {std::string("rootfs@2026.04.07"), std::string("app@1.2.17")},
          .rollback_token_armed = true,
          .auto_rollback_on_failure = true,
      },
  });

  assert_true(confirmation_result.state == BootConfirmationState::Confirmed &&
                  confirmation_result.is_valid(),
              "OTAWorkflowTest should reach confirm success after verify, compatibility, staging, switch, and frozen boot-success criteria all pass");
  assert_equal(1, static_cast<int>(boot_control_adapter.success_targets.size()),
               "OTAWorkflowTest should call mark_boot_success exactly once on the success path");
  assert_equal(0, static_cast<int>(boot_control_adapter.failed_targets.size()),
               "OTAWorkflowTest should not mark boot failed on the happy path");
}

}  // namespace

int main() {
  try {
    test_ota_workflow_integration_runs_apply_switch_confirm_success_through_ota_components();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}