#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ota/IOTAPackageVerifier.h"

namespace dasall::infra::ota {

struct TrustAnchorMaterial {
  std::string anchor_id;
  std::string algorithm;
  std::string key_format;
  std::string public_key_ref;
  std::string version_ref;
  std::string not_after;

  [[nodiscard]] bool is_valid() const {
    return !anchor_id.empty() && !algorithm.empty() && !key_format.empty() &&
           !public_key_ref.empty() && !version_ref.empty() && !not_after.empty();
  }
};

struct TrustAnchorLoadResult {
  bool loaded = false;
  TrustAnchorMaterial material;
  std::string detail;
};

class ITrustAnchorProvider {
 public:
  virtual ~ITrustAnchorProvider() = default;

  [[nodiscard]] virtual TrustAnchorLoadResult load_active_anchor(
      std::string_view anchor_purpose,
      std::string_view algorithm) const = 0;
};

struct PackageVerifierPolicy {
  bool verify_required = true;
  std::string signature_algorithm = "ed25519";
  std::uint64_t minimum_release_counter = 1;
  bool allow_downgrade = false;

  [[nodiscard]] bool is_valid() const {
    return !signature_algorithm.empty() && minimum_release_counter > 0;
  }
};

class IPackageVerifierPolicyProvider {
 public:
  virtual ~IPackageVerifierPolicyProvider() = default;

  [[nodiscard]] virtual PackageVerifierPolicy current_policy() const = 0;
};

struct PackageVerificationReport {
  bool signature_ok = false;
  bool hash_ok = false;
  std::uint64_t release_counter = 0;
  std::vector<std::string> hash_set;
  std::vector<std::string> compatible_profiles;
  std::vector<ArtifactDescriptor> artifact_list;

  [[nodiscard]] bool is_valid() const {
    return release_counter > 0 && !hash_set.empty() &&
           has_unique_non_empty_values(hash_set) && !compatible_profiles.empty() &&
           has_unique_non_empty_values(compatible_profiles) && !artifact_list.empty() &&
           std::all_of(artifact_list.begin(),
                       artifact_list.end(),
                       [](const ArtifactDescriptor& artifact) {
                         return artifact.is_valid();
                       });
  }
};

struct ArtifactVerificationReport {
  bool hash_ok = false;
  std::vector<std::string> verified_hashes;

  [[nodiscard]] bool is_valid() const {
    return hash_ok && !verified_hashes.empty() &&
           has_unique_non_empty_values(verified_hashes);
  }
};

class ISignatureVerifierAdapter {
 public:
  virtual ~ISignatureVerifierAdapter() = default;

  [[nodiscard]] virtual PackageVerificationReport verify_package(
      const PackageDescriptor& package_descriptor,
      std::string_view signature_algorithm,
      const TrustAnchorMaterial* trust_anchor) const = 0;

  [[nodiscard]] virtual ArtifactVerificationReport verify_artifact(
      const ArtifactDescriptor& artifact_descriptor) const = 0;
};

class PackageVerifier final : public IOTAPackageVerifier {
 public:
  struct Dependencies {
    const ITrustAnchorProvider* trust_anchor_provider = nullptr;
    const IPackageVerifierPolicyProvider* policy_provider = nullptr;
    const ISignatureVerifierAdapter* signature_verifier = nullptr;
  };

  explicit PackageVerifier(Dependencies dependencies);

  [[nodiscard]] PackageVerificationResult verify_package(
      const PackageDescriptor& package_descriptor) const override;
  [[nodiscard]] ArtifactVerificationResult verify_artifact(
      const ArtifactDescriptor& artifact_descriptor) const override;

 private:
  Dependencies dependencies_;
};

}  // namespace dasall::infra::ota