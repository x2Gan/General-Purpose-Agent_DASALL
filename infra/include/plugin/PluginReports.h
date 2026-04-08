#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "plugin/PluginManifest.h"

namespace dasall::infra::plugin {

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

[[nodiscard]] inline bool has_unique_non_empty_plugin_reason_codes(
    const std::vector<std::string>& reason_codes) {
  for (std::size_t index = 0; index < reason_codes.size(); ++index) {
    if (reason_codes[index].empty()) {
      return false;
    }

    for (std::size_t probe = index + 1; probe < reason_codes.size(); ++probe) {
      if (reason_codes[index] == reason_codes[probe]) {
        return false;
      }
    }
  }

  return true;
}

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

  [[nodiscard]] static SignatureReport failure(
      std::string algorithm,
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

struct CompatibilityReport {
  bool abi_ok = false;
  bool api_ok = false;
  bool dependency_ok = false;
  std::vector<std::string> reason_codes;
  std::string resolved_platform_tag = std::string(kPluginUnknownValue);
  std::string required_abi = std::string(kPluginUnknownValue);
  std::string evidence_ref;

  [[nodiscard]] static CompatibilityReport success(std::string resolved_platform_tag,
                                                   std::string required_abi,
                                                   std::string evidence_ref) {
    return CompatibilityReport{
        .abi_ok = true,
        .api_ok = true,
        .dependency_ok = true,
        .reason_codes = {},
        .resolved_platform_tag = plugin_value_or_unknown(resolved_platform_tag),
        .required_abi = plugin_value_or_unknown(required_abi),
        .evidence_ref = std::move(evidence_ref),
    };
  }

  [[nodiscard]] static CompatibilityReport failure(bool abi_ok,
                                                   bool api_ok,
                                                   bool dependency_ok,
                                                   std::vector<std::string> reason_codes,
                                                   std::string resolved_platform_tag,
                                                   std::string required_abi,
                                                   std::string evidence_ref) {
    return CompatibilityReport{
        .abi_ok = abi_ok,
        .api_ok = api_ok,
        .dependency_ok = dependency_ok,
        .reason_codes = std::move(reason_codes),
        .resolved_platform_tag = plugin_value_or_unknown(resolved_platform_tag),
        .required_abi = plugin_value_or_unknown(required_abi),
        .evidence_ref = std::move(evidence_ref),
    };
  }

  [[nodiscard]] bool is_valid() const {
    if (resolved_platform_tag == kPluginUnknownValue ||
        !is_plugin_manifest_required_abi(required_abi) || evidence_ref.empty() ||
        !has_unique_non_empty_plugin_reason_codes(reason_codes)) {
      return false;
    }

    if (abi_ok && api_ok && dependency_ok) {
      return reason_codes.empty();
    }

    return !reason_codes.empty();
  }
};

}  // namespace dasall::infra::plugin