#include <exception>
#include <iostream>
#include <string>

#include "ota/IOTAPackageVerifier.h"
#include "support/TestAssertions.h"

namespace {

dasall::infra::ota::PackageDescriptor make_valid_package_descriptor() {
  return dasall::infra::ota::PackageDescriptor{
      .package_id = std::string("pkg-ota-003"),
      .package_uri = std::string("https://updates.example.invalid/pkg-ota-003.raucb"),
      .manifest_version = std::string("1"),
      .package_kind = std::string("bundle"),
      .signed_metadata_ref = std::string("targets/v1/pkg-ota-003.json"),
      .size_bytes = 8192,
  };
}

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

class NullOTAPackageVerifier final : public dasall::infra::ota::IOTAPackageVerifier {
 public:
  explicit NullOTAPackageVerifier(std::string signature_algorithm = "ed25519",
                                  std::string trust_anchor_ref = "secret://ota/root")
      : signature_algorithm_(std::move(signature_algorithm)),
        trust_anchor_ref_(std::move(trust_anchor_ref)) {}

  [[nodiscard]] dasall::infra::ota::PackageVerificationResult verify_package(
      const dasall::infra::ota::PackageDescriptor& package_descriptor) const override {
    if (!package_descriptor.is_valid()) {
      return dasall::infra::ota::PackageVerificationResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "package descriptor must stay fully specified",
          "ota.verify_package",
          "NullOTAPackageVerifier");
    }

    if ((signature_algorithm_ != "ed25519" &&
         signature_algorithm_ != "ecdsa-p256-sha256") ||
        trust_anchor_ref_.empty()) {
      return dasall::infra::ota::PackageVerificationResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "signature algorithm and trust anchor must stay inside the frozen design set",
          "ota.verify_package",
          "NullOTAPackageVerifier");
    }

    return dasall::infra::ota::PackageVerificationResult::success(
        dasall::infra::ota::VerifiedPackageManifest{
            .package_id = package_descriptor.package_id,
            .signature_ok = true,
            .hash_set = {std::string("sha256:pkg-003")},
            .release_counter = 12,
            .compatible_profiles = {std::string("desktop_full")},
            .artifact_list = {make_valid_artifact_descriptor()},
        });
  }

  [[nodiscard]] dasall::infra::ota::ArtifactVerificationResult verify_artifact(
      const dasall::infra::ota::ArtifactDescriptor& artifact_descriptor) const override {
    if (!artifact_descriptor.is_valid()) {
      return dasall::infra::ota::ArtifactVerificationResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "artifact descriptor must keep frozen hardware selector and artifact class constraints",
          "ota.verify_artifact",
          "NullOTAPackageVerifier");
    }

    return dasall::infra::ota::ArtifactVerificationResult::success(
        artifact_descriptor,
        {std::string("sha256:artifact-003")});
  }

 private:
  std::string signature_algorithm_;
  std::string trust_anchor_ref_;
};

void test_ota_package_verifier_interface_accepts_frozen_package_and_artifact_inputs() {
  using dasall::tests::support::assert_true;

  NullOTAPackageVerifier verifier;

  const auto package_result = verifier.verify_package(make_valid_package_descriptor());
  assert_true(package_result.verified && package_result.manifest.is_valid(),
              "IOTAPackageVerifier should expose a verified manifest payload when package metadata, algorithm, and trust anchor are valid");
  assert_true(package_result.references_only_contract_error_types(),
              "successful package verification should keep failure typing inside contracts semantics only");

  const auto artifact_result = verifier.verify_artifact(make_valid_artifact_descriptor());
  assert_true(artifact_result.verified && artifact_result.references_only_contract_error_types(),
              "IOTAPackageVerifier should expose a single-artifact verification result without leaking trust anchor internals");
}

void test_ota_package_verifier_interface_reports_invalid_inputs_and_blocker_regressions() {
  using dasall::tests::support::assert_true;

  NullOTAPackageVerifier invalid_algorithm_verifier("sha1", "secret://ota/root");
  const auto package_failure = invalid_algorithm_verifier.verify_package(
      make_valid_package_descriptor());
  assert_true(!package_failure.verified,
              "IOTAPackageVerifier should reject algorithms outside the frozen allow-list");
  assert_true(package_failure.references_only_contract_error_types(),
              "signature-algorithm failures should stay within contracts ResultCode/ErrorInfo types");

  NullOTAPackageVerifier verifier;
  const auto artifact_failure = verifier.verify_artifact(dasall::infra::ota::ArtifactDescriptor{});
  assert_true(!artifact_failure.verified,
              "IOTAPackageVerifier should reject unspecified artifact descriptors rather than silently bypass verification");
  assert_true(artifact_failure.references_only_contract_error_types(),
              "artifact verification failures should remain contract-shaped and observable");
}

}  // namespace

int main() {
  try {
    test_ota_package_verifier_interface_accepts_frozen_package_and_artifact_inputs();
    test_ota_package_verifier_interface_reports_invalid_inputs_and_blocker_regressions();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}