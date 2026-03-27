#pragma once

#include <optional>
#include <string>

#include "BuildProfileManifest.h"
#include "IProfileCatalog.h"

namespace dasall::profiles {

struct BuildProfileResolveRequest {
  std::string profile_id;
  std::optional<std::string> expected_target_platform;

  [[nodiscard]] bool has_consistent_values() const {
    return !profile_id.empty();
  }
};

struct BuildProfileResolveResult {
  std::optional<BuildProfileManifest> manifest;
  std::optional<ProfileErrorCode> error_code;

  [[nodiscard]] bool ok() const {
    return manifest.has_value() && !error_code.has_value();
  }

  [[nodiscard]] bool has_consistent_values() const {
    if (!manifest.has_value()) {
      return error_code.has_value();
    }

    return !error_code.has_value() && manifest->has_consistent_values();
  }
};

class BuildProfileResolver {
 public:
  explicit BuildProfileResolver(const IProfileCatalog& catalog);

  [[nodiscard]] BuildProfileResolveResult resolve_build_manifest(
      const BuildProfileResolveRequest& request) const;

 private:
  const IProfileCatalog& catalog_;
};

}  // namespace dasall::profiles
