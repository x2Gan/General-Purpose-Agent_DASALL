#include <exception>
#include <iostream>
#include <string>
#include <utility>

#include "ota/PackageVerifier.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

using dasall::infra::ota::ArtifactClass;
using dasall::infra::ota::ArtifactDescriptor;
using dasall::infra::ota::ArtifactVerificationReport;
using dasall::infra::ota::IPackageVerifierPolicyProvider;
using dasall::infra::ota::ISignatureVerifierAdapter;
using dasall::infra::ota::ITrustAnchorProvider;
using dasall::infra::ota::PackageDescriptor;
using dasall::infra::ota::PackageVerificationReport;
using dasall::infra::ota::PackageVerifier;
using dasall::infra::ota::PackageVerifierPolicy;
using dasall::infra::ota::TrustAnchorLoadResult;
using dasall::infra::ota::TrustAnchorMaterial;

PackageDescriptor make_valid_package_descriptor() {
  return PackageDescriptor{
      .package_id = std::string("pkg-ota-007"),
      .package_uri = std::string("https://updates.example.invalid/pkg-ota-007.raucb"),
      .manifest_version = std::string("1"),
      .package_kind = std::string("bundle"),
      .signed_metadata_ref = std::string("targets/v1/pkg-ota-007.json"),
      .size_bytes = 16384,
  };
}

ArtifactDescriptor make_valid_artifact_descriptor() {
  return ArtifactDescriptor{
      .artifact_id = std::string("artifact-rootfs-b"),
      .artifact_class = ArtifactClass::SlotBound,
      .target_slot_group = std::string("rootfs"),
      .version = std::string("1.2.7"),
      .hardware_selector = {std::string("board-a")},
      .dependency_refs = {},
  };
}

PackageVerificationReport make_valid_package_report() {
  return PackageVerificationReport{
      .signature_ok = true,
      .hash_ok = true,
      .release_counter = 12,
      .hash_set = {std::string("sha256:pkg-007")},
      .compatible_profiles = {std::string("desktop_full")},
      .artifact_list = {make_valid_artifact_descriptor()},
  };
}

class FakeTrustAnchorProvider final : public ITrustAnchorProvider {
 public:
  explicit FakeTrustAnchorProvider(TrustAnchorLoadResult result)
      : result_(std::move(result)) {}

  [[nodiscard]] TrustAnchorLoadResult load_active_anchor(
      std::string_view,
      std::string_view) const override {
    return result_;
  }

 private:
  TrustAnchorLoadResult result_;
};

class FakePolicyProvider final : public IPackageVerifierPolicyProvider {
 public:
  explicit FakePolicyProvider(PackageVerifierPolicy policy)
      : policy_(std::move(policy)) {}

  [[nodiscard]] PackageVerifierPolicy current_policy() const override {
    return policy_;
  }

 private:
  PackageVerifierPolicy policy_;
};

class FakeSignatureVerifier final : public ISignatureVerifierAdapter {
 public:
  FakeSignatureVerifier(PackageVerificationReport package_report,
                        ArtifactVerificationReport artifact_report)
      : package_report_(std::move(package_report)),
        artifact_report_(std::move(artifact_report)) {}

  [[nodiscard]] PackageVerificationReport verify_package(
      const PackageDescriptor&,
      std::string_view,
      const TrustAnchorMaterial*) const override {
    return package_report_;
  }

  [[nodiscard]] ArtifactVerificationReport verify_artifact(
      const ArtifactDescriptor&) const override {
    return artifact_report_;
  }

 private:
  PackageVerificationReport package_report_;
  ArtifactVerificationReport artifact_report_;
};

void test_package_verifier_accepts_valid_anchor_signature_hash_and_artifact_inputs() {
  using dasall::tests::support::assert_true;

  const FakeTrustAnchorProvider trust_anchor_provider(TrustAnchorLoadResult{
      .loaded = true,
      .material = TrustAnchorMaterial{
          .anchor_id = std::string("anchor-ota-007"),
          .algorithm = std::string("ed25519"),
          .key_format = std::string("pem"),
          .public_key_ref = std::string("secret://ota/root"),
          .version_ref = std::string("anchor-v3"),
          .not_after = std::string("2027-04-01T00:00:00Z"),
      },
      .detail = std::string("loaded"),
  });
  const FakePolicyProvider policy_provider(PackageVerifierPolicy{
      .verify_required = true,
      .signature_algorithm = std::string("ed25519"),
      .minimum_release_counter = 10,
      .allow_downgrade = false,
  });
  const FakeSignatureVerifier signature_verifier(
      make_valid_package_report(),
      ArtifactVerificationReport{
          .hash_ok = true,
          .verified_hashes = {std::string("sha256:artifact-007")},
      });

  const PackageVerifier verifier(PackageVerifier::Dependencies{
      .trust_anchor_provider = &trust_anchor_provider,
      .policy_provider = &policy_provider,
      .signature_verifier = &signature_verifier,
  });

  const auto package_result = verifier.verify_package(make_valid_package_descriptor());
  assert_true(package_result.verified && package_result.manifest.is_valid() &&
                  package_result.references_only_contract_error_types(),
              "PackageVerifier should emit a valid VerifiedPackageManifest when trust anchor, signature, hash, and release_counter all satisfy the frozen gate");

  const auto artifact_result = verifier.verify_artifact(make_valid_artifact_descriptor());
  assert_true(artifact_result.verified &&
                  artifact_result.references_only_contract_error_types(),
              "PackageVerifier should emit verified artifact hashes without leaking trust anchor internals to the public boundary");
}

void test_package_verifier_rejects_signature_hash_and_release_counter_regressions() {
  using dasall::tests::support::assert_true;

  const FakeTrustAnchorProvider trust_anchor_provider(TrustAnchorLoadResult{
      .loaded = true,
      .material = TrustAnchorMaterial{
          .anchor_id = std::string("anchor-ota-007"),
          .algorithm = std::string("ed25519"),
          .key_format = std::string("pem"),
          .public_key_ref = std::string("secret://ota/root"),
          .version_ref = std::string("anchor-v3"),
          .not_after = std::string("2027-04-01T00:00:00Z"),
      },
      .detail = std::string("loaded"),
  });
  const FakePolicyProvider policy_provider(PackageVerifierPolicy{
      .verify_required = true,
      .signature_algorithm = std::string("ed25519"),
      .minimum_release_counter = 10,
      .allow_downgrade = false,
  });

  auto signature_failure_report = make_valid_package_report();
  signature_failure_report.signature_ok = false;
  const FakeSignatureVerifier signature_failure_verifier(
      signature_failure_report,
      ArtifactVerificationReport{
          .hash_ok = true,
          .verified_hashes = {std::string("sha256:artifact-007")},
      });
  const PackageVerifier signature_failure_subject(PackageVerifier::Dependencies{
      .trust_anchor_provider = &trust_anchor_provider,
      .policy_provider = &policy_provider,
      .signature_verifier = &signature_failure_verifier,
  });
  const auto signature_failure =
      signature_failure_subject.verify_package(make_valid_package_descriptor());
  assert_true(!signature_failure.verified &&
                  signature_failure.references_only_contract_error_types(),
              "PackageVerifier should short-circuit before install when signature verification fails");

  auto hash_failure_report = make_valid_package_report();
  hash_failure_report.hash_ok = false;
  const FakeSignatureVerifier hash_failure_verifier(
      hash_failure_report,
      ArtifactVerificationReport{
          .hash_ok = false,
          .verified_hashes = {},
      });
  const PackageVerifier hash_failure_subject(PackageVerifier::Dependencies{
      .trust_anchor_provider = &trust_anchor_provider,
      .policy_provider = &policy_provider,
      .signature_verifier = &hash_failure_verifier,
  });
  const auto hash_failure =
      hash_failure_subject.verify_package(make_valid_package_descriptor());
  assert_true(!hash_failure.verified &&
                  hash_failure.references_only_contract_error_types(),
              "PackageVerifier should reject package metadata when the hash check fails");

  auto rollback_report = make_valid_package_report();
  rollback_report.release_counter = 7;
  const FakeSignatureVerifier rollback_verifier(
      rollback_report,
      ArtifactVerificationReport{
          .hash_ok = true,
          .verified_hashes = {std::string("sha256:artifact-007")},
      });
  const PackageVerifier rollback_subject(PackageVerifier::Dependencies{
      .trust_anchor_provider = &trust_anchor_provider,
      .policy_provider = &policy_provider,
      .signature_verifier = &rollback_verifier,
  });
  const auto rollback_failure =
      rollback_subject.verify_package(make_valid_package_descriptor());
  assert_true(!rollback_failure.verified &&
                  rollback_failure.references_only_contract_error_types(),
              "PackageVerifier should reject release_counter rollback before the OTA workflow can enter install");

  const auto artifact_failure =
      hash_failure_subject.verify_artifact(make_valid_artifact_descriptor());
  assert_true(!artifact_failure.verified &&
                  artifact_failure.references_only_contract_error_types(),
              "PackageVerifier should surface artifact hash failures through the public artifact verification result");
}

}  // namespace

int main() {
  try {
    test_package_verifier_accepts_valid_anchor_signature_hash_and_artifact_inputs();
    test_package_verifier_rejects_signature_hash_and_release_counter_regressions();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}