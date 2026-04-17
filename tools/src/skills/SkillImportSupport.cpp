#include "skills/SkillImportSupport.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace dasall::tools::skills {

namespace {

[[nodiscard]] std::size_t count_indent(const std::string& line) {
  std::size_t indent = 0U;
  while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t')) {
    ++indent;
  }

  return indent;
}

[[nodiscard]] std::string strip_optional_quotes(std::string value) {
  if (value.size() >= 2U &&
      ((value.front() == '"' && value.back() == '"') ||
       (value.front() == '\'' && value.back() == '\''))) {
    return value.substr(1U, value.size() - 2U);
  }

  return value;
}

[[nodiscard]] std::string strip_inline_comment(std::string value) {
  const auto comment_pos = value.find('#');
  if (comment_pos == std::string::npos) {
    return strip_optional_quotes(trim_copy(std::move(value)));
  }

  return strip_optional_quotes(trim_copy(value.substr(0U, comment_pos)));
}

[[nodiscard]] std::string join_path(
    const std::vector<std::pair<std::size_t, std::string>>& path,
    const std::string& leaf_key) {
  std::ostringstream stream;
  bool first = true;

  for (const auto& node : path) {
    if (!first) {
      stream << '.';
    }
    stream << node.second;
    first = false;
  }

  if (!leaf_key.empty()) {
    if (!first) {
      stream << '.';
    }
    stream << leaf_key;
  }

  return stream.str();
}

[[nodiscard]] bool path_is_within(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate) {
  const auto normalized_root = root.lexically_normal();
  const auto normalized_candidate = candidate.lexically_normal();
  auto candidate_part = normalized_candidate.begin();
  for (auto root_part = normalized_root.begin(); root_part != normalized_root.end();
       ++root_part, ++candidate_part) {
    if (candidate_part == normalized_candidate.end() || *root_part != *candidate_part) {
      return false;
    }
  }

  return true;
}

}  // namespace

std::string trim_copy(std::string value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }

  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1U);
}

ParsedKeyValueDocument parse_key_value_document(std::string_view text) {
  ParsedKeyValueDocument parsed;

  std::vector<std::pair<std::size_t, std::string>> path_stack;
  std::istringstream stream{std::string(text)};
  std::string raw_line;
  while (std::getline(stream, raw_line)) {
    const auto trimmed = trim_copy(raw_line);
    if (trimmed.empty() || trimmed.starts_with('#')) {
      continue;
    }

    const auto indent = count_indent(raw_line);
    if (trimmed.starts_with("- ")) {
      while (!path_stack.empty() && path_stack.back().first >= indent) {
        path_stack.pop_back();
      }

      if (path_stack.empty()) {
        parsed.error = "yaml list item is missing parent key";
        return parsed;
      }

      parsed.list_values[join_path(path_stack, "")].push_back(
          strip_inline_comment(trimmed.substr(2U)));
      continue;
    }

    const auto colon = trimmed.find(':');
    if (colon == std::string::npos) {
      parsed.error = "yaml line missing colon separator";
      return parsed;
    }

    const auto key = trim_copy(trimmed.substr(0U, colon));
    const auto value = strip_inline_comment(trimmed.substr(colon + 1U));

    while (!path_stack.empty() && path_stack.back().first >= indent) {
      path_stack.pop_back();
    }

    if (value.empty()) {
      path_stack.emplace_back(indent, key);
      continue;
    }

    parsed.scalar_values[join_path(path_stack, key)] = value;
  }

  parsed.ok = true;
  return parsed;
}

ParsedKeyValueDocument parse_key_value_yaml_file(const std::filesystem::path& yaml_path) {
  ParsedKeyValueDocument parsed;

  std::ifstream stream(yaml_path);
  if (!stream.is_open()) {
    parsed.error = "unable to open yaml file";
    return parsed;
  }

  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return parse_key_value_document(buffer.str());
}

std::filesystem::path resolve_import_path(
    const std::filesystem::path& project_root,
    const std::filesystem::path& base_dir,
    const std::filesystem::path& candidate) {
  if (candidate.is_absolute()) {
    return candidate.lexically_normal();
  }

  const auto project_candidate = (project_root / candidate).lexically_normal();
  if (!project_root.empty() && std::filesystem::exists(project_candidate)) {
    return project_candidate;
  }

  return (base_dir / candidate).lexically_normal();
}

std::string make_asset_ref(
    const std::filesystem::path& project_root,
    const std::filesystem::path& candidate) {
  const auto normalized_candidate = candidate.lexically_normal();
  if (!project_root.empty() && path_is_within(project_root, normalized_candidate)) {
    return std::filesystem::relative(normalized_candidate, project_root).generic_string();
  }

  return normalized_candidate.generic_string();
}

std::string normalize_identifier(std::string_view value) {
  std::string normalized;
  normalized.reserve(value.size());

  bool previous_was_separator = false;
  for (const auto character : value) {
    const auto unsigned_character = static_cast<unsigned char>(character);
    if (std::isalnum(unsigned_character) != 0) {
      normalized.push_back(
          static_cast<char>(std::tolower(unsigned_character)));
      previous_was_separator = false;
      continue;
    }

    if (!previous_was_separator) {
      normalized.push_back('.');
      previous_was_separator = true;
    }
  }

  while (!normalized.empty() && normalized.front() == '.') {
    normalized.erase(normalized.begin());
  }
  while (!normalized.empty() && normalized.back() == '.') {
    normalized.pop_back();
  }

  return normalized;
}

std::vector<std::string> tokenize_identifier(std::string_view value) {
  std::vector<std::string> tokens;
  std::unordered_set<std::string> seen;
  std::string current;

  for (const auto character : value) {
    const auto unsigned_character = static_cast<unsigned char>(character);
    if (std::isalnum(unsigned_character) != 0) {
      current.push_back(static_cast<char>(std::tolower(unsigned_character)));
      continue;
    }

    if (!current.empty() && seen.insert(current).second) {
      tokens.push_back(current);
    }
    current.clear();
  }

  if (!current.empty() && seen.insert(current).second) {
    tokens.push_back(current);
  }

  return tokens;
}

std::vector<std::string> unique_non_empty_values(const std::vector<std::string>& values) {
  std::vector<std::string> unique_values;
  std::unordered_set<std::string> seen;

  for (auto value : values) {
    value = trim_copy(std::move(value));
    if (value.empty()) {
      continue;
    }

    if (seen.insert(value).second) {
      unique_values.push_back(std::move(value));
    }
  }

  return unique_values;
}

bool dialect_is_internal(const std::optional<std::string>& dialect_ref) {
  if (!dialect_ref.has_value()) {
    return true;
  }

  const auto normalized = normalize_identifier(*dialect_ref);
  return normalized.starts_with("internal") || normalized.starts_with("dasall.skill");
}

}  // namespace dasall::tools::skills