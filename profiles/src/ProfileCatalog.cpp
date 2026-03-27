#include "ProfileCatalog.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ProfileError.h"
#include "ProfileYamlParser.h"

namespace dasall::profiles {
namespace {

[[nodiscard]] std::string trim_copy(std::string value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }

  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1U);
}

[[nodiscard]] std::optional<std::string> parse_profile_id_from_cmake(
    const std::filesystem::path& profile_cmake_path) {
  std::ifstream stream(profile_cmake_path);
  if (!stream.is_open()) {
    return std::nullopt;
  }

  std::string line;
  while (std::getline(stream, line)) {
    const std::string trimmed = trim_copy(line);
    if (!trimmed.starts_with("set(DASALL_PROFILE_NAME")) {
      continue;
    }

    const std::size_t first_quote = trimmed.find('"');
    if (first_quote == std::string::npos) {
      continue;
    }

    const std::size_t second_quote = trimmed.find('"', first_quote + 1U);
    if (second_quote == std::string::npos || second_quote <= first_quote + 1U) {
      continue;
    }

    return trimmed.substr(first_quote + 1U, second_quote - first_quote - 1U);
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<ProfileDescriptor> load_descriptor_from_directory(
    const std::filesystem::path& directory_path) {
  const std::filesystem::path profile_cmake_path = directory_path / "profile.cmake";
  const std::filesystem::path runtime_policy_path = directory_path / "runtime_policy.yaml";

  if (!std::filesystem::exists(profile_cmake_path) || !std::filesystem::exists(runtime_policy_path)) {
    return std::nullopt;
  }

  const ParsedProfileYaml parsed_yaml = parse_profile_yaml_file(runtime_policy_path);
  if (!parsed_yaml.ok) {
    return std::nullopt;
  }

  const auto profile_id_it = parsed_yaml.scalar_values.find("profile_meta.profile_id");
  const auto schema_version_it = parsed_yaml.scalar_values.find("schema_version");
  const auto target_platform_it = parsed_yaml.scalar_values.find("profile_meta.target_platform");
  const auto support_level_it = parsed_yaml.scalar_values.find("profile_meta.support_level");
  if (profile_id_it == parsed_yaml.scalar_values.end() ||
      schema_version_it == parsed_yaml.scalar_values.end() ||
      target_platform_it == parsed_yaml.scalar_values.end() ||
      support_level_it == parsed_yaml.scalar_values.end()) {
    return std::nullopt;
  }

  const std::optional<std::string> profile_id_from_cmake =
      parse_profile_id_from_cmake(profile_cmake_path);
  if (!profile_id_from_cmake.has_value() || *profile_id_from_cmake != profile_id_it->second) {
    return std::nullopt;
  }

  ProfileDescriptor descriptor{
      .profile_id = profile_id_it->second,
      .schema_version = schema_version_it->second,
      .target_platform = target_platform_it->second,
      .asset_paths = ProfileAssetPaths{
          .profile_root = directory_path,
          .profile_cmake_path = profile_cmake_path,
          .runtime_policy_path = runtime_policy_path,
      },
      .support_level = support_level_it->second,
  };

  if (!descriptor.has_consistent_values()) {
    return std::nullopt;
  }

  return descriptor;
}

}  // namespace

ProfileCatalog::ProfileCatalog(std::filesystem::path profiles_root)
    : profiles_root_(std::move(profiles_root)) {}

ProfileCatalogListResult ProfileCatalog::list_profiles() const {
  ProfileCatalogListResult result;

  if (!std::filesystem::exists(profiles_root_) || !std::filesystem::is_directory(profiles_root_)) {
    result.error_code = ProfileErrorCode::CatalogUnavailable;
    return result;
  }

  std::vector<ProfileDescriptor> descriptors;
  try {
    for (const auto& entry : std::filesystem::directory_iterator(profiles_root_)) {
      if (!entry.is_directory()) {
        continue;
      }

      const auto descriptor = load_descriptor_from_directory(entry.path());
      if (!descriptor.has_value()) {
        continue;
      }

      descriptors.push_back(*descriptor);
    }
  } catch (...) {
    result.error_code = ProfileErrorCode::CatalogUnavailable;
    return result;
  }

  std::sort(descriptors.begin(), descriptors.end(),
            [](const ProfileDescriptor& lhs, const ProfileDescriptor& rhs) {
              return lhs.profile_id < rhs.profile_id;
            });

  const bool has_duplicate_id =
      std::adjacent_find(descriptors.begin(), descriptors.end(),
                         [](const ProfileDescriptor& lhs, const ProfileDescriptor& rhs) {
                           return lhs.profile_id == rhs.profile_id;
                         }) != descriptors.end();

  if (descriptors.empty() || has_duplicate_id) {
    result.error_code = ProfileErrorCode::CatalogUnavailable;
    return result;
  }

  result.profiles = std::move(descriptors);
  return result;
}

ProfileCatalogLookupResult ProfileCatalog::get_profile(std::string_view profile_id) const {
  const ProfileCatalogListResult listed = list_profiles();
  if (!listed.ok()) {
    return ProfileCatalogLookupResult{
        .profile = std::nullopt,
        .error_code = listed.error_code,
    };
  }

  const auto it = std::find_if(listed.profiles.begin(), listed.profiles.end(),
                               [profile_id](const ProfileDescriptor& descriptor) {
                                 return descriptor.profile_id == profile_id;
                               });

  if (it == listed.profiles.end()) {
    return ProfileCatalogLookupResult{
        .profile = std::nullopt,
        .error_code = ProfileErrorCode::ProfileNotFound,
    };
  }

  return ProfileCatalogLookupResult{
      .profile = *it,
      .error_code = std::nullopt,
  };
}

}  // namespace dasall::profiles
