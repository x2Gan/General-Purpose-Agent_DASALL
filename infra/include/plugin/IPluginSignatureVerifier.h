#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "plugin/PluginManifest.h"
#include "plugin/PluginReports.h"

namespace dasall::infra::plugin {

inline constexpr std::string_view kPluginSignatureAnchorPurpose = "plugin.package.verify";

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

class IPluginSignatureVerifier {
 public:
  virtual ~IPluginSignatureVerifier() = default;

  [[nodiscard]] virtual SignatureReport verify(
      const PluginSignatureVerificationRequest& request) const = 0;
};

}  // namespace dasall::infra::plugin