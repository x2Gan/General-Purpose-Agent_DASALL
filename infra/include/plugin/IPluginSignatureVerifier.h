#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "plugin/PluginManifest.h"

namespace dasall::infra::plugin {

inline constexpr std::string_view kPluginSignatureAnchorPurpose = "plugin.package.verify";

enum class PluginSignatureChainStatus {
  Unknown = 0,
  Verified = 1,
  AnchorMissing = 2,
  AlgorithmUnsupported = 3,
  SignatureInvalid = 4,
  CertificateExpired = 5,
  TrustLevelTooLow = 6,
  RollbackRejected = 7,
};

inline constexpr std::string_view plugin_signature_chain_status_name(
    PluginSignatureChainStatus chain_status) {
  switch (chain_status) {
    case PluginSignatureChainStatus::Unknown:
      return "unknown";
    case PluginSignatureChainStatus::Verified:
      return "verified";
    case PluginSignatureChainStatus::AnchorMissing:
      return "anchor_missing";
    case PluginSignatureChainStatus::AlgorithmUnsupported:
      return "algorithm_unsupported";
    case PluginSignatureChainStatus::SignatureInvalid:
      return "signature_invalid";
    case PluginSignatureChainStatus::CertificateExpired:
      return "certificate_expired";
    case PluginSignatureChainStatus::TrustLevelTooLow:
      return "trust_level_too_low";
    case PluginSignatureChainStatus::RollbackRejected:
      return "rollback_rejected";
  }

  return "unknown";
}

[[nodiscard]] inline bool is_plugin_signature_algorithm_allowed(std::string_view algorithm) {
  return algorithm == "ed25519" || algorithm == "ecdsa-p256-sha256";
}

[[nodiscard]] inline bool plugin_trust_level_meets_minimum(PluginTrustLevel actual,
                                                           PluginTrustLevel minimum) {
  return actual != PluginTrustLevel::Unknown && minimum != PluginTrustLevel::Unknown &&
         static_cast<int>(actual) >= static_cast<int>(minimum);
}

struct PluginTrustAnchorMaterial {
  std::string anchor_ref = std::string(kPluginUnknownValue);
  std::string anchor_purpose = std::string(kPluginSignatureAnchorPurpose);
  std::vector<std::string> allowed_algorithms = {std::string("ed25519"),
                                                 std::string("ecdsa-p256-sha256")};
  PluginTrustLevel minimum_trust_level = PluginTrustLevel::Internal;
  std::string last_known_good_version = std::string(kPluginUnknownValue);

  [[nodiscard]] bool has_valid_allowed_algorithms() const {
    if (allowed_algorithms.empty()) {
      return false;
    }

    for (std::size_t index = 0; index < allowed_algorithms.size(); ++index) {
      if (!is_plugin_signature_algorithm_allowed(allowed_algorithms[index])) {
        return false;
      }

      for (std::size_t probe = index + 1; probe < allowed_algorithms.size(); ++probe) {
        if (allowed_algorithms[index] == allowed_algorithms[probe]) {
          return false;
        }
      }
    }

    return true;
  }

  [[nodiscard]] bool is_valid() const {
    return !anchor_ref.empty() && anchor_ref != kPluginUnknownValue &&
           anchor_purpose == kPluginSignatureAnchorPurpose && has_valid_allowed_algorithms() &&
           minimum_trust_level != PluginTrustLevel::Unknown &&
           (last_known_good_version == kPluginUnknownValue ||
            is_plugin_manifest_semver(last_known_good_version));
  }
};

struct PluginSignatureVerificationRequest {
  PluginManifest manifest;
  std::string package_ref = std::string(kPluginUnknownValue);
  std::string signature_algorithm = std::string(kPluginUnknownValue);
  PluginTrustAnchorMaterial trust_anchor;

  [[nodiscard]] bool is_valid() const {
    return manifest.is_valid() && !package_ref.empty() && package_ref != kPluginUnknownValue &&
           !signature_algorithm.empty() && signature_algorithm != kPluginUnknownValue &&
           trust_anchor.is_valid();
  }
};

struct SignatureReport {
  bool verified = false;
  std::string signer = std::string(kPluginUnknownValue);
  std::string algorithm = std::string(kPluginUnknownValue);
  PluginSignatureChainStatus chain_status = PluginSignatureChainStatus::Unknown;
  PluginTrustLevel inferred_trust_level = PluginTrustLevel::Unknown;
  std::string reason_code = std::string(kPluginUnknownValue);
  std::string evidence_ref;

  [[nodiscard]] static SignatureReport success(std::string signer,
                                               std::string algorithm,
                                               PluginTrustLevel inferred_trust_level,
                                               std::string evidence_ref,
                                               std::string reason_code = "signature_verified") {
    return SignatureReport{
        .verified = true,
        .signer = plugin_value_or_unknown(signer),
        .algorithm = plugin_value_or_unknown(algorithm),
        .chain_status = PluginSignatureChainStatus::Verified,
        .inferred_trust_level = inferred_trust_level,
        .reason_code = plugin_value_or_unknown(reason_code),
        .evidence_ref = std::move(evidence_ref),
    };
  }

  [[nodiscard]] static SignatureReport failure(std::string algorithm,
                                               PluginSignatureChainStatus chain_status,
                                               PluginTrustLevel inferred_trust_level,
                                               std::string reason_code,
                                               std::string evidence_ref,
                                               std::string signer = std::string(kPluginUnknownValue)) {
    return SignatureReport{
        .verified = false,
        .signer = plugin_value_or_unknown(signer),
        .algorithm = plugin_value_or_unknown(algorithm),
        .chain_status = chain_status,
        .inferred_trust_level = inferred_trust_level,
        .reason_code = plugin_value_or_unknown(reason_code),
        .evidence_ref = std::move(evidence_ref),
    };
  }

  [[nodiscard]] bool is_valid() const {
    if (algorithm == kPluginUnknownValue || reason_code == kPluginUnknownValue ||
        chain_status == PluginSignatureChainStatus::Unknown || evidence_ref.empty()) {
      return false;
    }

    if (verified) {
      return signer != kPluginUnknownValue &&
             is_plugin_signature_algorithm_allowed(algorithm) &&
             chain_status == PluginSignatureChainStatus::Verified &&
             inferred_trust_level != PluginTrustLevel::Unknown;
    }

    if (chain_status != PluginSignatureChainStatus::AlgorithmUnsupported &&
        !is_plugin_signature_algorithm_allowed(algorithm)) {
      return false;
    }

    return chain_status != PluginSignatureChainStatus::Verified;
  }
};

class IPluginSignatureVerifier {
 public:
  virtual ~IPluginSignatureVerifier() = default;

  [[nodiscard]] virtual SignatureReport verify(
      const PluginSignatureVerificationRequest& request) const = 0;
};

}  // namespace dasall::infra::plugin