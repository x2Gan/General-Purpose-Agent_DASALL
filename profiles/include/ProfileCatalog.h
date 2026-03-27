#pragma once

#include <filesystem>

#include "IProfileCatalog.h"

namespace dasall::profiles {

class ProfileCatalog final : public IProfileCatalog {
 public:
  explicit ProfileCatalog(std::filesystem::path profiles_root = "profiles");

  [[nodiscard]] ProfileCatalogListResult list_profiles() const override;
  [[nodiscard]] ProfileCatalogLookupResult get_profile(std::string_view profile_id) const override;

 private:
  std::filesystem::path profiles_root_;
};

}  // namespace dasall::profiles
