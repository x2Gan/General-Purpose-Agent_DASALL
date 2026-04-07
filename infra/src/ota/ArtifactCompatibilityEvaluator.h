#pragma once

#include <string>
#include <vector>

#include "ota/OTATypes.h"

namespace dasall::infra::ota {

struct DeviceCapabilitySnapshot {
  std::vector<std::string> supported_hardware;
  std::vector<std::string> available_dependency_refs;

  [[nodiscard]] bool is_valid() const {
    return !supported_hardware.empty() &&
           has_unique_non_empty_values(supported_hardware) &&
           has_unique_non_empty_values(available_dependency_refs);
  }
};

struct ArtifactCompatibilityProfile {
  std::string profile_name;
  bool slot_bound_allowed = true;
  bool repo_bound_allowed = true;
  std::vector<std::string> forbidden_dependency_refs;

  [[nodiscard]] bool is_valid() const {
    return !profile_name.empty() &&
           has_unique_non_empty_values(forbidden_dependency_refs);
  }
};

struct CompatibilityReport {
  bool compatible = false;
  std::vector<std::string> accepted_artifact_ids;
  std::vector<contracts::ErrorInfo> blocking_reasons;

  [[nodiscard]] bool uses_contract_error_types_only() const {
    return std::all_of(blocking_reasons.begin(),
                       blocking_reasons.end(),
                       [](const contracts::ErrorInfo& error_info) {
                         return is_contract_error_info_populated(error_info);
                       });
  }

  [[nodiscard]] bool is_valid() const {
    if (compatible) {
      return !accepted_artifact_ids.empty() &&
             has_unique_non_empty_values(accepted_artifact_ids) &&
             blocking_reasons.empty();
    }

    return accepted_artifact_ids.empty() && !blocking_reasons.empty() &&
           uses_contract_error_types_only();
  }
};

class ArtifactCompatibilityEvaluator {
 public:
  [[nodiscard]] CompatibilityReport evaluate(
      const VerifiedPackageManifest& verified_manifest,
      const DeviceCapabilitySnapshot& capability,
      const ArtifactCompatibilityProfile& profile) const;
};

}  // namespace dasall::infra::ota