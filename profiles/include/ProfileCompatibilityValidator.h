#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "BuildProfileManifest.h"
#include "RuntimePolicySnapshot.h"
#include "ValidationReport.h"

namespace dasall::profiles {

struct ProfileRuntimeEnvironment {
  std::string target_platform;
  std::vector<std::string> available_modules;
  std::vector<std::string> available_adapters;

  [[nodiscard]] bool has_consistent_values() const {
    return !target_platform.empty();
  }
};

class IProfileCompatibilityValidator {
 public:
  virtual ~IProfileCompatibilityValidator() = default;

  virtual ValidationReport validate(const RuntimePolicySnapshot& candidate,
                                    const BuildProfileManifest& build_manifest,
                                    const ProfileRuntimeEnvironment& environment) const = 0;
};

class ProfileCompatibilityValidator final : public IProfileCompatibilityValidator {
 public:
  ValidationReport validate(const RuntimePolicySnapshot& candidate,
                            const BuildProfileManifest& build_manifest,
                            const ProfileRuntimeEnvironment& environment) const override;
};

}  // namespace dasall::profiles
