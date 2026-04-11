#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dasall::llm::detail {

struct ParsedKeyValueYaml {
  std::unordered_map<std::string, std::string> scalar_values;
  std::unordered_map<std::string, std::vector<std::string>> list_values;
  bool ok = false;
  std::string error;
};

inline std::string trim_copy(std::string value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }

  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1U);
}

inline std::size_t count_indent(const std::string& line) {
  std::size_t indent = 0U;
  while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t')) {
    ++indent;
  }

  return indent;
}

inline std::string strip_optional_quotes(std::string value) {
  if (value.size() >= 2U &&
      ((value.front() == '"' && value.back() == '"') ||
       (value.front() == '\'' && value.back() == '\''))) {
    return value.substr(1U, value.size() - 2U);
  }

  return value;
}

inline std::string strip_inline_comment(std::string value) {
  const auto comment_pos = value.find('#');
  if (comment_pos == std::string::npos) {
    return strip_optional_quotes(trim_copy(std::move(value)));
  }

  return strip_optional_quotes(trim_copy(value.substr(0U, comment_pos)));
}

inline std::string join_path(const std::vector<std::pair<std::size_t, std::string>>& path,
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

inline ParsedKeyValueYaml parse_key_value_yaml_file(const std::filesystem::path& yaml_path) {
  ParsedKeyValueYaml parsed;

  std::ifstream stream(yaml_path);
  if (!stream.is_open()) {
    parsed.error = "unable to open yaml file";
    return parsed;
  }

  std::vector<std::pair<std::size_t, std::string>> path_stack;
  std::string raw_line;
  while (std::getline(stream, raw_line)) {
    const std::string trimmed = trim_copy(raw_line);
    if (trimmed.empty() || trimmed.starts_with('#')) {
      continue;
    }

    const std::size_t indent = count_indent(raw_line);

    if (trimmed.starts_with("- ")) {
      while (!path_stack.empty() && path_stack.back().first >= indent) {
        path_stack.pop_back();
      }

      if (path_stack.empty()) {
        parsed.error = "yaml list item is missing parent key";
        return parsed;
      }

      const std::string list_key = join_path(path_stack, "");
      parsed.list_values[list_key].push_back(strip_inline_comment(trimmed.substr(2U)));
      continue;
    }

    const auto colon = trimmed.find(':');
    if (colon == std::string::npos) {
      parsed.error = "yaml line missing colon separator";
      return parsed;
    }

    const std::string key = trim_copy(trimmed.substr(0U, colon));
    const std::string value = strip_inline_comment(trimmed.substr(colon + 1U));

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

}  // namespace dasall::llm::detail