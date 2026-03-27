#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "dasall/profiles/ProfileDescriptor.h"

namespace dasall::profiles {

enum class ProfileErrorCode : std::uint16_t;

struct ProfileCatalogListResult {
  std::vector<ProfileDescriptor> profiles;
  // Implementations should return PRF_E_CATALOG_UNAVAILABLE on discovery failures.
  std::optional<ProfileErrorCode> error_code;

  [[nodiscard]] bool ok() const {
    return !error_code.has_value();
  }

  [[nodiscard]] bool has_consistent_values() const {
    if (!ok()) {
      return profiles.empty();
    }

    return std::all_of(profiles.begin(), profiles.end(), [](const ProfileDescriptor& profile) {
      return profile.has_consistent_values();
    }) && has_unique_profile_ids();
  }

  [[nodiscard]] bool has_unique_profile_ids() const {
    std::vector<std::string_view> profile_ids;
    profile_ids.reserve(profiles.size());
    for (const ProfileDescriptor& profile : profiles) {
      profile_ids.push_back(profile.profile_id);
    }

    std::sort(profile_ids.begin(), profile_ids.end());
    return std::adjacent_find(profile_ids.begin(), profile_ids.end()) == profile_ids.end();
  }
};

struct ProfileCatalogLookupResult {
  std::optional<ProfileDescriptor> profile;
  // Implementations should return PRF_E_PROFILE_NOT_FOUND for missing ids.
  std::optional<ProfileErrorCode> error_code;

  [[nodiscard]] bool ok() const {
    return profile.has_value() && !error_code.has_value();
  }

  [[nodiscard]] bool has_consistent_values() const {
    if (!profile.has_value()) {
      return error_code.has_value();
    }

    return !error_code.has_value() && profile->has_consistent_values();
  }
};

class IProfileCatalog {
 public:
  virtual ~IProfileCatalog() = default;

  // Returns all discoverable profiles; failures are reported through error_code.
  virtual ProfileCatalogListResult list_profiles() const = 0;
  // Returns one descriptor for profile_id; missing ids are reported through error_code.
  virtual ProfileCatalogLookupResult get_profile(std::string_view profile_id) const = 0;
};

}  // namespace dasall::profiles