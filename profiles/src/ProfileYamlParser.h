#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace dasall::profiles {

struct ParsedProfileYaml {
  std::unordered_map<std::string, std::string> scalar_values;
  std::unordered_map<std::string, std::vector<std::string>> list_values;
  bool ok = false;
  std::string error;
};

[[nodiscard]] ParsedProfileYaml parse_profile_yaml_file(const std::filesystem::path& yaml_path);

}  // namespace dasall::profiles
