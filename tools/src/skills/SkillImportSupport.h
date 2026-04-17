#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dasall::tools::skills {

struct ParsedKeyValueDocument {
  std::unordered_map<std::string, std::string> scalar_values;
  std::unordered_map<std::string, std::vector<std::string>> list_values;
  bool ok = false;
  std::string error;
};

[[nodiscard]] std::string trim_copy(std::string value);
[[nodiscard]] ParsedKeyValueDocument parse_key_value_document(std::string_view text);
[[nodiscard]] ParsedKeyValueDocument parse_key_value_yaml_file(
    const std::filesystem::path& yaml_path);
[[nodiscard]] std::filesystem::path resolve_import_path(
    const std::filesystem::path& project_root,
    const std::filesystem::path& base_dir,
    const std::filesystem::path& candidate);
[[nodiscard]] std::string make_asset_ref(
    const std::filesystem::path& project_root,
    const std::filesystem::path& candidate);
[[nodiscard]] std::string normalize_identifier(std::string_view value);
[[nodiscard]] std::vector<std::string> tokenize_identifier(std::string_view value);
[[nodiscard]] std::vector<std::string> unique_non_empty_values(
    const std::vector<std::string>& values);
[[nodiscard]] bool dialect_is_internal(
    const std::optional<std::string>& dialect_ref);

}  // namespace dasall::tools::skills