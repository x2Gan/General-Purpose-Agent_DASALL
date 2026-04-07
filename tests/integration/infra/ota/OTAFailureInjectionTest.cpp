#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "health/IHealthMonitor.h"
#include "ota/BootConfirmationMonitor.h"
#include "ota/PackageVerifier.h"
#include "ota/RollbackController.h"
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
using dasall::infra::ota::ArtifactDescriptor;
using dasall::infra::ota::ArtifactVerificationReport;
using dasall::infra::ota::BootConfirmationMonitor;
using dasall::infra::ota::BootConfirmationPolicySnapshot;
using dasall::infra::ota::BootConfirmationRequest;
using dasall::infra::ota::BootConfirmationState;
using dasall::infra::ota::BootMutationResult;
using dasall::infra::ota::BootSuccessSignal;
using dasall::infra::ota::BootTargetQueryResult;
using dasall::infra::ota::HeartbeatFreshnessReport;
using dasall::infra::ota::IBootControlAdapter;
using dasall::infra::ota::IBootSuccessSignalProvider;
using dasall::infra::ota::IHeartbeatFreshnessSource;
using dasall::infra::ota::IPackageVerifierPolicyProvider;
using dasall::infra::ota::IRepoPointerRecoveryAdapter;
using dasall::infra::ota::IRollbackEvidenceWriter;
using dasall::infra::ota::ISignatureVerifierAdapter;
using dasall::infra::ota::ITrustAnchorProvider;
using dasall::infra::ota::ITimeProvider;
using dasall::infra::ota::IVersionReportSource;
using dasall::infra::ota::InstallEvidence;
using dasall::infra::ota::PackageDescriptor;
using dasall::infra::ota::PackageVerificationReport;
using dasall::infra::ota::PackageVerifier;
using dasall::infra::ota::PackageVerifierPolicy;
using dasall::infra::ota::RepoPointerRecoveryResult;
using dasall::infra::ota::RollbackController;
using dasall::infra::ota::RollbackEvidenceResult;
using dasall::infra::ota::RollbackToken;
using dasall::infra::ota::SlotPlan;
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

BootConfirmationRequest make_confirmation_request() {
  return BootConfirmationRequest{
      .plan_id = std::string("ota-plan-017"),
      .package_id = std::string("ota-pkg-017"),
      .slot_plan = SlotPlan{
          .active_slot = std::string("rootfs_a"),
          .target_slot = std::string("rootfs_b"),
          .slot_group = std::string("rootfs"),
          .switch_policy = std::string("confirm_after_boot"),
          .confirm_deadline = std::string("2026-04-07T19:20:00Z"),
      },
      .rollback_token = RollbackToken{
          .rollback_id = std::string("rollback-017"),
          .previous_boot_target = std::string("rootfs_a"),
          .staged_artifacts = {std::string("artifact-rootfs-017")},
          .created_at = std::string("2026-04-07T19:00:00Z"),
          .expires_at = std::string("2026-04-07T19:30:00Z"),
      },
      .policy = BootConfirmationPolicySnapshot{
          .health_blocking_components = {std::string("boot-agent")},
          .required_heartbeat_entities = {std::string("boot-agent")},
          .expected_slot_versions = {std::string("rootfs@2026.04.07")},
          .rollback_token_armed = true,
          .auto_rollback_on_failure = true,
      },
  };
}

std::vector<InstallEvidence> make_install_evidence() {
  return {
      InstallEvidence{
          .artifact_id = std::string("artifact-rootfs-017"),
          .written_target = std::string("/dev/mmcblk0p3"),
          .checksum = std::string("sha256:artifact-rootfs-017"),
          .install_ts = std::string("2026-04-07T19:00:20Z"),
          .installer_version = std::string("install-executor/1.0"),
      },
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

class FailingSignatureVerifier final : public ISignatureVerifierAdapter {
 public:
  [[nodiscard]] PackageVerificationReport verify_package(
      const PackageDescriptor&,
      std::string_view,
      const TrustAnchorMaterial*) const override {
    return PackageVerificationReport{
        .signature_ok = false,
        .hash_ok = true,
        .release_counter = 17,
        .hash_set = {std::string("sha256:pkg-017")},
        .compatible_profiles = {std::string("desktop_full")},
        .artifact_list = {make_slot_artifact()},
    };
  }

  [[nodiscard]] ArtifactVerificationReport verify_artifact(
      const ArtifactDescriptor&) const override {
    return ArtifactVerificationReport{
        .hash_ok = true,
        .verified_hashes = {std::string("sha256:artifact-rootfs-017")},
    };
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
    return BootMutationResult::success(std::string(target), std::string("mark_boot_success"));
  }

  [[nodiscard]] BootMutationResult mark_boot_failed(std::string_view target) override {
    failed_targets.emplace_back(target);
    return BootMutationResult::success(std::string(target), std::string("mark_boot_failed"));
  }

  std::vector<std::string> failed_targets;

 private:
  std::string active_target_;
  std::string next_boot_target_;
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
        .slot_bound_versions = {std::string("rootfs@2026.04.07")},
        .detail_ref = std::string("version://ota/confirm/match"),
        .version = 17,
        .observed_ts = 1712516402000,
    };
  }
};

class FailingRepoPointerRecoveryAdapter final : public IRepoPointerRecoveryAdapter {
 public:
  mutable std::size_t calls = 0;

  [[nodiscard]] RepoPointerRecoveryResult recover(
      const RollbackToken&,
      const std::vector<InstallEvidence>&) const override {
    ++calls;
    return RepoPointerRecoveryResult::failure(
        dasall::contracts::ResultCode::RuntimeRetryExhausted,
        std::string("repo pointer recovery failed"),
        std::string("ota.rollback.recover_repo_pointer"),
        std::string("FailingRepoPointerRecoveryAdapter"));
  }
};

class FakeRollbackEvidenceWriter final : public IRollbackEvidenceWriter {
 public:
  mutable std::size_t calls = 0;

  [[nodiscard]] RollbackEvidenceResult write(
      const RollbackToken&,
      std::string_view,
      const std::vector<std::string>&) const override {
    ++calls;
    return RollbackEvidenceResult::success(std::string("audit://ota/rollback/017"));
  }
};

class FakeTimeProvider final : public ITimeProvider {
 public:
  [[nodiscard]] std::string now_utc() const override {
    return std::string("2026-04-07T19:10:00Z");
  }
};

void test_ota_failure_injection_surfaces_verify_fail_before_apply() {
  using dasall::tests::support::assert_true;

  FakeTrustAnchorProvider trust_anchor_provider;
  FakePolicyProvider policy_provider;
  FailingSignatureVerifier signature_verifier;
  const PackageVerifier verifier(PackageVerifier::Dependencies{
      .trust_anchor_provider = &trust_anchor_provider,
      .policy_provider = &policy_provider,
      .signature_verifier = &signature_verifier,
  });

  const auto result = verifier.verify_package(make_package_descriptor());
  assert_true(!result.verified && result.references_only_contract_error_types(),
              "OTAFailureInjectionTest should surface verify_fail before the OTA workflow can enter staging or slot switching");
}

void test_ota_failure_injection_surfaces_confirm_timeout_as_boot_failed() {
  using dasall::contracts::ResultCode;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  FakeBootControlAdapter boot_control_adapter("rootfs_b");
  FakeHealthMonitor health_monitor;
  FakeSuccessSignalProvider success_signal_provider;
  FakeHeartbeatFreshnessSource heartbeat_source;
  FakeVersionReportSource version_report_source;
  BootConfirmationMonitor monitor(BootConfirmationMonitor::Dependencies{
      .boot_control_adapter = &boot_control_adapter,
      .health_monitor = &health_monitor,
      .success_signal_provider = &success_signal_provider,
      .heartbeat_freshness_source = &heartbeat_source,
      .version_report_source = &version_report_source,
  });

  const auto result = monitor.handle_timeout(make_confirmation_request());
  assert_true(result.state == BootConfirmationState::TimedOut && result.is_valid() &&
                  result.result_code == ResultCode::ProviderTimeout,
              "OTAFailureInjectionTest should map confirm_timeout to the frozen provider-timeout outward code and mark boot failed");
  assert_equal(1, static_cast<int>(boot_control_adapter.failed_targets.size()),
               "OTAFailureInjectionTest should invoke mark_boot_failed exactly once on confirm timeout");
}

void test_ota_failure_injection_surfaces_rollback_fail_after_repo_recovery_failure() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  FakeBootControlAdapter boot_control_adapter("rootfs_b");
  FailingRepoPointerRecoveryAdapter repo_pointer_recovery_adapter;
  FakeRollbackEvidenceWriter evidence_writer;
  FakeTimeProvider time_provider;

  RollbackController rollback_controller(RollbackController::Dependencies{
      .boot_control_adapter = &boot_control_adapter,
      .repo_pointer_recovery_adapter = &repo_pointer_recovery_adapter,
      .evidence_writer = &evidence_writer,
      .time_provider = &time_provider,
  });

  const auto result = rollback_controller.rollback(
      make_confirmation_request().rollback_token,
      make_install_evidence());
  assert_true(!result.rolled_back && result.references_only_contract_error_types(),
              "OTAFailureInjectionTest should surface rollback_fail when repo pointer recovery cannot restore repo_bound state");
  assert_equal(1, static_cast<int>(repo_pointer_recovery_adapter.calls),
               "OTAFailureInjectionTest should attempt repo pointer recovery exactly once on rollback failure path");
  assert_equal(0, static_cast<int>(evidence_writer.calls),
               "OTAFailureInjectionTest should stop before writing rollback evidence when repo recovery already failed");
}

}  // namespace

int main() {
  try {
    test_ota_failure_injection_surfaces_verify_fail_before_apply();
    test_ota_failure_injection_surfaces_confirm_timeout_as_boot_failed();
    test_ota_failure_injection_surfaces_rollback_fail_after_repo_recovery_failure();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}