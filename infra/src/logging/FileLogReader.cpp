#include "logging/FileLogReader.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace dasall::infra::logging {

namespace {

namespace fs = std::filesystem;

[[nodiscard]] bool is_json_whitespace(char ch) {
  return ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t';
}

void skip_json_whitespace(std::string_view input, std::size_t& cursor) {
  while (cursor < input.size() && is_json_whitespace(input[cursor])) {
    ++cursor;
  }
}

[[nodiscard]] std::optional<std::string> parse_json_string(std::string_view input,
                                                           std::size_t& cursor) {
  skip_json_whitespace(input, cursor);
  if (cursor >= input.size() || input[cursor] != '"') {
    return std::nullopt;
  }

  ++cursor;
  std::string value;
  while (cursor < input.size()) {
    const char ch = input[cursor++];
    if (ch == '"') {
      return value;
    }

    if (ch != '\\') {
      value.push_back(ch);
      continue;
    }

    if (cursor >= input.size()) {
      return std::nullopt;
    }

    const char escaped = input[cursor++];
    switch (escaped) {
      case '"':
        value.push_back('"');
        break;
      case '\\':
        value.push_back('\\');
        break;
      case '/':
        value.push_back('/');
        break;
      case 'b':
        value.push_back('\b');
        break;
      case 'f':
        value.push_back('\f');
        break;
      case 'n':
        value.push_back('\n');
        break;
      case 'r':
        value.push_back('\r');
        break;
      case 't':
        value.push_back('\t');
        break;
      default:
        return std::nullopt;
    }
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> extract_json_string_field(
    std::string_view json,
    std::string_view key) {
  const std::string needle = std::string("\"") + std::string(key) + "\":";
  const auto position = json.find(needle);
  if (position == std::string_view::npos) {
    return std::nullopt;
  }

  std::size_t cursor = position + needle.size();
  return parse_json_string(json, cursor);
}

[[nodiscard]] std::optional<std::int64_t> extract_json_int64_field(std::string_view json,
                                                                   std::string_view key) {
  const std::string needle = std::string("\"") + std::string(key) + "\":";
  const auto position = json.find(needle);
  if (position == std::string_view::npos) {
    return std::nullopt;
  }

  std::size_t cursor = position + needle.size();
  skip_json_whitespace(json, cursor);
  const std::size_t start = cursor;
  while (cursor < json.size() && (json[cursor] == '-' || std::isdigit(static_cast<unsigned char>(json[cursor])) != 0)) {
    ++cursor;
  }

  if (start == cursor) {
    return std::nullopt;
  }

  std::int64_t value = 0;
  const auto result = std::from_chars(json.data() + start, json.data() + cursor, value);
  if (result.ec != std::errc()) {
    return std::nullopt;
  }

  return value;
}

[[nodiscard]] std::optional<LogEvent::AttributeMap> extract_attrs(std::string_view json) {
  const std::string needle = "\"attrs\":";
  const auto position = json.find(needle);
  if (position == std::string_view::npos) {
    return std::nullopt;
  }

  std::size_t cursor = position + needle.size();
  skip_json_whitespace(json, cursor);
  if (cursor >= json.size() || json[cursor] != '{') {
    return std::nullopt;
  }

  ++cursor;
  LogEvent::AttributeMap attrs;
  while (cursor < json.size()) {
    skip_json_whitespace(json, cursor);
    if (cursor < json.size() && json[cursor] == '}') {
      ++cursor;
      return attrs;
    }

    auto key = parse_json_string(json, cursor);
    if (!key.has_value()) {
      return std::nullopt;
    }

    skip_json_whitespace(json, cursor);
    if (cursor >= json.size() || json[cursor] != ':') {
      return std::nullopt;
    }
    ++cursor;

    auto value = parse_json_string(json, cursor);
    if (!value.has_value()) {
      return std::nullopt;
    }

    attrs.insert_or_assign(*key, *value);

    skip_json_whitespace(json, cursor);
    if (cursor < json.size() && json[cursor] == ',') {
      ++cursor;
      continue;
    }

    if (cursor < json.size() && json[cursor] == '}') {
      ++cursor;
      return attrs;
    }

    return std::nullopt;
  }

  return std::nullopt;
}

[[nodiscard]] LogLevel parse_level(std::string_view level_name) {
  if (level_name == "trace") {
    return LogLevel::Trace;
  }
  if (level_name == "debug") {
    return LogLevel::Debug;
  }
  if (level_name == "info") {
    return LogLevel::Info;
  }
  if (level_name == "warn") {
    return LogLevel::Warn;
  }
  if (level_name == "error") {
    return LogLevel::Error;
  }
  if (level_name == "fatal") {
    return LogLevel::Fatal;
  }

  return LogLevel::Unspecified;
}

[[nodiscard]] std::optional<LogEvent> parse_structured_log_line(std::string_view line) {
  const auto module = extract_json_string_field(line, "module");
  const auto message = extract_json_string_field(line, "message");
  const auto level = extract_json_string_field(line, "level");
  const auto ts = extract_json_int64_field(line, "ts_ms");
  const auto attrs = extract_attrs(line);
  if (!module.has_value() || !message.has_value() || !level.has_value() || !ts.has_value() ||
      !attrs.has_value()) {
    return std::nullopt;
  }

  return LogEvent{
      .level = parse_level(*level),
      .module = *module,
      .message = *message,
      .attrs = *attrs,
      .ts = *ts,
  };
}

[[nodiscard]] bool has_rotation_suffix(const fs::path& path,
                                       const fs::path& base_path,
                                       int& rotation_index) {
  const std::string base_name = base_path.filename().string();
  const std::string candidate = path.filename().string();
  if (candidate == base_name) {
    rotation_index = 0;
    return true;
  }

  const std::string prefix = base_name + ".";
  if (candidate.rfind(prefix, 0) != 0) {
    return false;
  }

  const std::string suffix = candidate.substr(prefix.size());
  if (suffix.empty()) {
    return false;
  }

  int value = 0;
  const auto result = std::from_chars(suffix.data(), suffix.data() + suffix.size(), value);
  if (result.ec != std::errc() || result.ptr != suffix.data() + suffix.size() || value <= 0) {
    return false;
  }

  rotation_index = value;
  return true;
}

}  // namespace

FileLogReader::FileLogReader(FileLogReaderOptions options) : options_(std::move(options)) {}

std::vector<LogEvent> FileLogReader::read_window(std::int64_t start_ts_ms,
                                                 std::int64_t end_ts_ms) {
  std::vector<LogEvent> records;
  if (!options_.has_consistent_values() || end_ts_ms < start_ts_ms) {
    return records;
  }

  for (const auto& path : collect_candidate_paths()) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
      continue;
    }

    std::string line;
    while (std::getline(stream, line)) {
      const auto parsed = parse_structured_log_line(line);
      if (!parsed.has_value() || !parsed->has_timestamp()) {
        continue;
      }

      if (*parsed->ts < start_ts_ms || *parsed->ts > end_ts_ms) {
        continue;
      }

      records.push_back(*parsed);
    }
  }

  return records;
}

std::vector<std::filesystem::path> FileLogReader::collect_candidate_paths() const {
  std::vector<std::pair<int, fs::path>> ordered_paths;
  const auto runtime_log_path = resolve_runtime_log_path();
  std::error_code error;
  if (fs::exists(runtime_log_path, error) && !error) {
    ordered_paths.emplace_back(0, runtime_log_path);
  }

  if (!options_.include_rotation_family) {
    std::vector<fs::path> paths;
    if (!ordered_paths.empty()) {
      paths.push_back(ordered_paths.front().second);
    }
    return paths;
  }

  error.clear();
  const auto parent_path = runtime_log_path.parent_path();
  if (parent_path.empty() || !fs::exists(parent_path, error) || error ||
      !fs::is_directory(parent_path, error)) {
    std::vector<fs::path> paths;
    for (const auto& [_, path] : ordered_paths) {
      paths.push_back(path);
    }
    return paths;
  }

  for (const auto& entry : fs::directory_iterator(parent_path, error)) {
    if (error || !entry.is_regular_file()) {
      continue;
    }

    int rotation_index = 0;
    if (!has_rotation_suffix(entry.path(), runtime_log_path, rotation_index) || rotation_index == 0) {
      continue;
    }

    ordered_paths.emplace_back(rotation_index, entry.path());
  }

  std::sort(ordered_paths.begin(),
            ordered_paths.end(),
            [](const auto& lhs, const auto& rhs) {
              if (lhs.first == 0) {
                return false;
              }
              if (rhs.first == 0) {
                return true;
              }
              return lhs.first > rhs.first;
            });

  std::vector<fs::path> paths;
  paths.reserve(ordered_paths.size());
  for (const auto& [_, path] : ordered_paths) {
    paths.push_back(path);
  }
  return paths;
}

std::filesystem::path FileLogReader::resolve_runtime_log_path() const {
  if (options_.runtime_log_path.is_absolute()) {
    return options_.runtime_log_path;
  }

  std::error_code error;
  const auto cwd = fs::current_path(error);
  if (error) {
    return options_.runtime_log_path;
  }

  return cwd / options_.runtime_log_path;
}

}  // namespace dasall::infra::logging