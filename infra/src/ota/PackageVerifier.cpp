#include "ota/PackageVerifier.h"

#include <string>
#include <utility>

#include "InfraErrorCode.h"

namespace dasall::infra::ota {
namespace {

constexpr char kPackageVerifierSourceRef[] = "PackageVerifier";
constexpr char kTrustAnchorPurpose[] = "ota.package.verify";

[[nodiscard]] bool is_supported_signature_algorithm(std::string_view algorithm) {
  return algorithm == "ed25519" || algorithm == "ecdsa-p256-sha256";
}

[[nodiscard]] PackageVerificationResult make_package_verify_failure(
    std::string message,
    std::string stage) {
  const auto mapping = map_infra_error_code(InfraErrorCode::OTAVerifyFail);
  return PackageVerificationResult::failure(mapping.result_code,
                                            std::move(message),
                                            std::move(stage),
                                            kPackageVerifierSourceRef);
}

[[nodiscard]] ArtifactVerificationResult make_artifact_verify_failure(
    std::string message,
    std::string stage) {
  const auto mapping = map_infra_error_code(InfraErrorCode::OTAVerifyFail);
  return ArtifactVerificationResult::failure(mapping.result_code,
                                             std::move(message),
                                             std::move(stage),
                                             kPackageVerifierSourceRef);
}

}  // namespace

PackageVerifier::PackageVerifier(Dependencies dependencies)
    : dependencies_(dependencies) {}

PackageVerificationResult PackageVerifier::verify_package(
    const PackageDescriptor& package_descriptor) const {
  if (!package_descriptor.is_valid()) {
    return PackageVerificationResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "package descriptor must stay fully specified before ota.package.verify",
        "ota.package.verify",
        kPackageVerifierSourceRef);
  }

  if (dependencies_.policy_provider == nullptr ||
      dependencies_.signature_verifier == nullptr) {
    return make_package_verify_failure(
        "package verifier requires policy and signature verifier dependencies",
        "ota.package.verify");
  }

  const auto policy = dependencies_.policy_provider->current_policy();
  if (!policy.is_valid()) {
    return PackageVerificationResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "package verifier policy must keep signature algorithm and release counter floor explicit",
        "ota.package.verify",
        kPackageVerifierSourceRef);
  }

  TrustAnchorMaterial trust_anchor;
  const TrustAnchorMaterial* trust_anchor_ptr = nullptr;
  if (policy.verify_required) {
    if (!is_supported_signature_algorithm(policy.signature_algorithm)) {
      return make_package_verify_failure(
          "signature algorithm is outside the frozen allow-list",
          "ota.package.verify");
    }

    if (dependencies_.trust_anchor_provider == nullptr) {
      return make_package_verify_failure(
          "trust anchor provider is required when package verification is enabled",
          "ota.package.verify");
    }

    const auto anchor_result = dependencies_.trust_anchor_provider->load_active_anchor(
        kTrustAnchorPurpose,
        policy.signature_algorithm);
    if (!anchor_result.loaded || !anchor_result.material.is_valid()) {
      return make_package_verify_failure(
          "trust anchor is unavailable for the configured signature algorithm",
          "ota.package.verify");
    }

    if (anchor_result.material.algorithm != policy.signature_algorithm) {
      return make_package_verify_failure(
          "trust anchor algorithm does not match the configured signature algorithm",
          "ota.package.verify");
    }

    trust_anchor = anchor_result.material;
    trust_anchor_ptr = &trust_anchor;
  }

  const auto report = dependencies_.signature_verifier->verify_package(
      package_descriptor,
      policy.signature_algorithm,
      trust_anchor_ptr);

  if (policy.verify_required && !report.signature_ok) {
    return make_package_verify_failure(
        "package signature verification failed",
        "ota.package.verify");
  }

  if (!report.hash_ok) {
    return make_package_verify_failure(
        "package hash verification failed",
        "ota.package.verify");
  }

  if (!report.is_valid()) {
    return PackageVerificationResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "verified package manifest must keep release_counter, hash_set, compatible_profiles, and artifact_list populated",
        "ota.package.verify",
        kPackageVerifierSourceRef);
  }

  if (!policy.allow_downgrade &&
      report.release_counter < policy.minimum_release_counter) {
    return make_package_verify_failure(
        "release_counter rolled back below the current monotonic floor",
        "ota.package.verify");
  }

  return PackageVerificationResult::success(VerifiedPackageManifest{
      .package_id = package_descriptor.package_id,
      .signature_ok = policy.verify_required ? report.signature_ok : true,
      .hash_set = report.hash_set,
      .release_counter = report.release_counter,
      .compatible_profiles = report.compatible_profiles,
      .artifact_list = report.artifact_list,
  });
}

ArtifactVerificationResult PackageVerifier::verify_artifact(
    const ArtifactDescriptor& artifact_descriptor) const {
  if (!artifact_descriptor.is_valid()) {
    return ArtifactVerificationResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "artifact descriptor must stay fully specified before ota.artifact.verify",
        "ota.artifact.verify",
        kPackageVerifierSourceRef);
  }

  if (dependencies_.signature_verifier == nullptr) {
    return make_artifact_verify_failure(
        "artifact verification requires a signature verifier dependency",
        "ota.artifact.verify");
  }

  const auto report = dependencies_.signature_verifier->verify_artifact(
      artifact_descriptor);
  if (!report.is_valid()) {
    return make_artifact_verify_failure(
        "artifact hash verification failed",
        "ota.artifact.verify");
  }

  return ArtifactVerificationResult::success(artifact_descriptor,
                                             report.verified_hashes);
}

}  // namespace dasall::infra::ota