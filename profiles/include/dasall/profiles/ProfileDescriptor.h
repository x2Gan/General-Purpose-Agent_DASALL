#pragma once

#include <filesystem>
#include <string>

namespace dasall::profiles {

struct ProfileAssetPaths {
  std::filesystem::path profile_root;
  std::filesystem::path profile_cmake_path;
  std::filesystem::path runtime_policy_path;

  [[nodiscard]] bool has_consistent_values() const {
    return !profile_root.empty() && !profile_cmake_path.empty() &&
           !runtime_policy_path.empty();
  }
};

struct ProfileDescriptor {
  std::string profile_id;
  std::string schema_version;
  std::string target_platform;
  ProfileAssetPaths asset_paths;
  std::string support_level;

  [[nodiscard]] bool has_consistent_values() const {
    return !profile_id.empty() && !schema_version.empty() && !target_platform.empty() &&
           asset_paths.has_consistent_values() && !support_level.empty();
  }
};

}  // namespace dasall::profiles