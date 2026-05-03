#include "DaemonEntryConfigLoader.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_map>

#include "DaemonProfileProjection.h"
#include "ProfileCatalog.h"
#include "RuntimePolicyProvider.h"

namespace dasall::apps::daemon {
namespace {

using KeyValueMap = std::unordered_map<std::string, std::string>;

struct DeploymentConfigParseResult {
  KeyValueMap values;
  std::string error;

  [[nodiscard]] bool ok() const {
    return error.empty();
  }
};

[[nodiscard]] std::string trim_copy(std::string value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }

  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1U);
}

[[nodiscard]] std::string strip_inline_comment(std::string value) {
  const auto comment_pos = value.find('#');
  if (comment_pos == std::string::npos) {
    return trim_copy(std::move(value));
  }

  return trim_copy(value.substr(0U, comment_pos));
}

[[nodiscard]] std::size_t count_indent(const std::string& line) {
  std::size_t indent = 0U;
  while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t')) {
    ++indent;
  }

  return indent;
}

[[nodiscard]] std::optional<bool> parse_bool(std::string_view value) {
  if (value == "true") {
    return true;
  }
  if (value == "false") {
    return false;
  }
  return std::nullopt;
}

template <typename Number>
[[nodiscard]] std::optional<Number> parse_number(std::string_view value) {
  try {
    if constexpr (std::is_same_v<Number, std::uint32_t>) {
      return static_cast<std::uint32_t>(std::stoul(std::string(value)));
    }

    if constexpr (std::is_same_v<Number, std::int32_t>) {
      return static_cast<std::int32_t>(std::stol(std::string(value)));
    }
  } catch (...) {
    return std::nullopt;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<DaemonStartupMode> parse_startup_mode(
    std::string_view value) {
  if (value == "direct_bind") {
    return DaemonStartupMode::DirectBind;
  }
  if (value == "socket_activated") {
    return DaemonStartupMode::SocketActivated;
  }
  return std::nullopt;
}

[[nodiscard]] DeploymentConfigParseResult parse_yaml_config(
    const std::filesystem::path& file_path) {
  DeploymentConfigParseResult result;

  std::ifstream stream(file_path);
  if (!stream.is_open()) {
    result.error = "unable to open deployment yaml";
    return result;
  }

  bool inside_daemon = false;
  std::size_t daemon_indent = 0U;
  std::string raw_line;
  while (std::getline(stream, raw_line)) {
    const std::string trimmed = trim_copy(raw_line);
    if (trimmed.empty() || trimmed.starts_with('#')) {
      continue;
    }

    const std::size_t indent = count_indent(raw_line);
    if (trimmed == "daemon:") {
      inside_daemon = true;
      daemon_indent = indent;
      continue;
    }

    if (!inside_daemon) {
      continue;
    }

    if (indent <= daemon_indent) {
      inside_daemon = false;
      continue;
    }

    const auto colon = trimmed.find(':');
    if (colon == std::string::npos) {
      result.error = "deployment yaml line missing colon";
      return result;
    }

    const std::string key = trim_copy(trimmed.substr(0U, colon));
    const std::string value = strip_inline_comment(trimmed.substr(colon + 1U));
    if (value.empty()) {
      result.error = "deployment yaml only supports scalar daemon keys in v1";
      return result;
    }

    result.values["daemon." + key] = value;
  }

  if (result.values.empty()) {
    result.error = "deployment yaml does not contain daemon overrides";
  }

  return result;
}

[[nodiscard]] std::size_t skip_json_whitespace(const std::string& content,
                                               std::size_t position) {
  while (position < content.size() &&
         std::isspace(static_cast<unsigned char>(content[position])) != 0) {
    ++position;
  }
  return position;
}

[[nodiscard]] std::optional<std::string> parse_json_string(
    const std::string& content,
    std::size_t* position) {
  if (*position >= content.size() || content[*position] != '"') {
    return std::nullopt;
  }

  ++(*position);
  std::ostringstream value;
  while (*position < content.size()) {
    const char current = content[*position];
    if (current == '"') {
      ++(*position);
      return value.str();
    }

    if (current == '\\') {
      ++(*position);
      if (*position >= content.size()) {
        return std::nullopt;
      }
    }

    value << content[*position];
    ++(*position);
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> parse_json_value(
    const std::string& content,
    std::size_t* position) {
  *position = skip_json_whitespace(content, *position);
  if (*position >= content.size()) {
    return std::nullopt;
  }

  if (content[*position] == '"') {
    return parse_json_string(content, position);
  }

  const std::size_t value_start = *position;
  while (*position < content.size() && content[*position] != ',' &&
         content[*position] != '}') {
    ++(*position);
  }

  return trim_copy(content.substr(value_start, *position - value_start));
}

[[nodiscard]] std::optional<std::size_t> find_matching_brace(
    const std::string& content,
    const std::size_t open_brace) {
  std::size_t depth = 0U;
  for (std::size_t index = open_brace; index < content.size(); ++index) {
    if (content[index] == '{') {
      ++depth;
      continue;
    }

    if (content[index] == '}') {
      if (depth == 0U) {
        return std::nullopt;
      }

      --depth;
      if (depth == 0U) {
        return index;
      }
    }
  }

  return std::nullopt;
}

[[nodiscard]] DeploymentConfigParseResult parse_json_config(
    const std::filesystem::path& file_path) {
  DeploymentConfigParseResult result;

  std::ifstream stream(file_path);
  if (!stream.is_open()) {
    result.error = "unable to open deployment json";
    return result;
  }

  std::ostringstream buffer;
  buffer << stream.rdbuf();
  const std::string content = buffer.str();
  const std::size_t daemon_key = content.find("\"daemon\"");
  if (daemon_key == std::string::npos) {
    result.error = "deployment json does not contain daemon object";
    return result;
  }

  std::size_t object_start = content.find('{', daemon_key);
  if (object_start == std::string::npos) {
    result.error = "deployment json daemon object is malformed";
    return result;
  }

  const auto object_end = find_matching_brace(content, object_start);
  if (!object_end.has_value()) {
    result.error = "deployment json daemon object is unterminated";
    return result;
  }

  std::size_t position = object_start + 1U;
  while (position < *object_end) {
    position = skip_json_whitespace(content, position);
    if (position >= *object_end) {
      break;
    }

    if (content[position] == ',') {
      ++position;
      continue;
    }

    const auto key = parse_json_string(content, &position);
    if (!key.has_value()) {
      result.error = "deployment json key parse failed";
      return result;
    }

    position = skip_json_whitespace(content, position);
    if (position >= *object_end || content[position] != ':') {
      result.error = "deployment json key is missing value separator";
      return result;
    }

    ++position;
    const auto value = parse_json_value(content, &position);
    if (!value.has_value()) {
      result.error = "deployment json value parse failed";
      return result;
    }

    result.values["daemon." + *key] = *value;
  }

  if (result.values.empty()) {
    result.error = "deployment json does not contain daemon overrides";
  }

  return result;
}

[[nodiscard]] DeploymentConfigParseResult parse_deployment_config(
    const std::filesystem::path& file_path) {
  const std::string extension = file_path.extension().string();
  if (extension == ".yaml" || extension == ".yml") {
    return parse_yaml_config(file_path);
  }

  if (extension == ".json") {
    return parse_json_config(file_path);
  }

  return DeploymentConfigParseResult{
      .values = {},
      .error = "unsupported deployment config format",
  };
}

[[nodiscard]] std::string profile_error_message(
    const std::optional<dasall::profiles::ProfileErrorCode>& error_code,
    std::string_view fallback) {
  if (!error_code.has_value()) {
    return std::string(fallback);
  }

  return std::string(fallback) + " error_code=" +
         std::to_string(static_cast<int>(*error_code));
}

[[nodiscard]] std::optional<std::string> overlay_config_value(
    const KeyValueMap& values,
    std::string_view key) {
  const auto it = values.find(std::string(key));
  if (it == values.end() || it->second.empty()) {
    return std::nullopt;
  }

  return it->second;
}

[[nodiscard]] bool apply_deployment_overrides(const KeyValueMap& values,
                                              DaemonBootstrapConfig* config,
                                              std::string* error) {
  if (const auto socket_path = overlay_config_value(values, "daemon.socket_path");
      socket_path.has_value()) {
    config->socket_path = *socket_path;
  }

  if (const auto listen_backlog = overlay_config_value(values, "daemon.listen_backlog");
      listen_backlog.has_value()) {
    const auto parsed = parse_number<std::uint32_t>(*listen_backlog);
    if (!parsed.has_value()) {
      *error = "daemon.listen_backlog must be an unsigned integer";
      return false;
    }
    config->listen_backlog = *parsed;
  }

  if (const auto max_payload_bytes = overlay_config_value(values, "daemon.max_payload_bytes");
      max_payload_bytes.has_value()) {
    const auto parsed = parse_number<std::uint32_t>(*max_payload_bytes);
    if (!parsed.has_value()) {
      *error = "daemon.max_payload_bytes must be an unsigned integer";
      return false;
    }
    config->max_payload_bytes = *parsed;
  }

  if (const auto dispatch_timeout_ms =
          overlay_config_value(values, "daemon.dispatch_timeout_ms");
      dispatch_timeout_ms.has_value()) {
    const auto parsed = parse_number<std::int32_t>(*dispatch_timeout_ms);
    if (!parsed.has_value()) {
      *error = "daemon.dispatch_timeout_ms must be an integer";
      return false;
    }
    config->dispatch_timeout_ms = *parsed;
  }

  if (const auto shutdown_grace_ms =
          overlay_config_value(values, "daemon.shutdown_grace_ms");
      shutdown_grace_ms.has_value()) {
    const auto parsed = parse_number<std::int32_t>(*shutdown_grace_ms);
    if (!parsed.has_value()) {
      *error = "daemon.shutdown_grace_ms must be an integer";
      return false;
    }
    config->shutdown_grace_ms = *parsed;
  }

  if (const auto receipt_ttl_sec = overlay_config_value(values, "daemon.receipt_ttl_sec");
      receipt_ttl_sec.has_value()) {
    const auto parsed = parse_number<std::int32_t>(*receipt_ttl_sec);
    if (!parsed.has_value()) {
      *error = "daemon.receipt_ttl_sec must be an integer";
      return false;
    }
    config->receipt_ttl_sec = *parsed;
  }

  if (const auto accept_workers = overlay_config_value(values, "daemon.accept_workers");
      accept_workers.has_value()) {
    const auto parsed = parse_number<std::uint32_t>(*accept_workers);
    if (!parsed.has_value()) {
      *error = "daemon.accept_workers must be an unsigned integer";
      return false;
    }
    config->accept_workers = *parsed;
  }

  if (const auto dispatch_workers = overlay_config_value(values, "daemon.dispatch_workers");
      dispatch_workers.has_value()) {
    const auto parsed = parse_number<std::uint32_t>(*dispatch_workers);
    if (!parsed.has_value()) {
      *error = "daemon.dispatch_workers must be an unsigned integer";
      return false;
    }
    config->dispatch_workers = *parsed;
  }

  if (const auto diag_enabled = overlay_config_value(values, "daemon.diag_enabled");
      diag_enabled.has_value()) {
    const auto parsed = parse_bool(*diag_enabled);
    if (!parsed.has_value()) {
      *error = "daemon.diag_enabled must be true or false";
      return false;
    }
    config->diag_enabled = *parsed;
  }

  if (const auto override_enabled =
          overlay_config_value(values, "daemon.override_enabled");
      override_enabled.has_value()) {
    const auto parsed = parse_bool(*override_enabled);
    if (!parsed.has_value()) {
      *error = "daemon.override_enabled must be true or false";
      return false;
    }
    config->override_enabled = *parsed;
  }

  if (const auto watchdog_enabled =
          overlay_config_value(values, "daemon.watchdog_enabled");
      watchdog_enabled.has_value()) {
    const auto parsed = parse_bool(*watchdog_enabled);
    if (!parsed.has_value()) {
      *error = "daemon.watchdog_enabled must be true or false";
      return false;
    }
    config->watchdog_enabled = *parsed;
  }

  if (const auto log_level = overlay_config_value(values, "daemon.log_level");
      log_level.has_value()) {
    config->log_level = *log_level;
  }

  if (const auto log_format = overlay_config_value(values, "daemon.log_format");
      log_format.has_value()) {
    config->log_format = *log_format;
  }

  if (const auto startup_mode = overlay_config_value(values, "daemon.startup_mode");
      startup_mode.has_value()) {
    const auto parsed = parse_startup_mode(*startup_mode);
    if (!parsed.has_value()) {
      *error = "daemon.startup_mode must be direct_bind or socket_activated";
      return false;
    }
    config->startup_mode = *parsed;
  }

  return true;
}

[[nodiscard]] std::string build_config_revision(
    const std::string& effective_profile_id,
    const std::uint64_t generation,
    const std::optional<std::filesystem::path>& deployment_config_path) {
  std::ostringstream revision;
  revision << "profile=" << effective_profile_id
           << ";generation=" << generation;
  if (deployment_config_path.has_value()) {
    revision << ";deployment=" << deployment_config_path->lexically_normal().string();
  }
  return revision.str();
}

}  // namespace

DaemonEntryConfigLoadResult DaemonEntryConfigLoader::load(
    const DaemonEntryConfigLoadRequest& request) const {
  if (!request.has_consistent_values()) {
    return DaemonEntryConfigLoadResult{
        .entry_config = std::nullopt,
        .message = "daemon entry config request is incomplete",
    };
  }

  const dasall::profiles::ProfileCatalog catalog(request.profiles_root);
  const dasall::profiles::DaemonProfileProjection projection(catalog);
  const auto projected = projection.load(
      dasall::profiles::DaemonProfileProjectionRequest{
          .profile_id = request.requested_profile_id,
      });
  if (!projected.ok() || !projected.settings.has_value()) {
    return DaemonEntryConfigLoadResult{
        .entry_config = std::nullopt,
        .message = profile_error_message(
            projected.error_code,
            "daemon profile projection failed for requested profile"),
    };
  }

  const dasall::profiles::RuntimePolicyProvider runtime_policy_provider(catalog);
  const auto runtime_snapshot = runtime_policy_provider.load_snapshot(
      dasall::profiles::RuntimePolicyLoadRequest{
          .profile_id = request.requested_profile_id,
      });
  if (!runtime_snapshot.ok() || !runtime_snapshot.snapshot) {
    return DaemonEntryConfigLoadResult{
        .entry_config = std::nullopt,
        .message = profile_error_message(
            runtime_snapshot.error_code,
            "runtime policy snapshot load failed for requested profile"),
    };
  }

  DaemonBootstrapConfig bootstrap_config;
  bootstrap_config.socket_path = projected.settings->socket_path;
  bootstrap_config.listen_backlog = projected.settings->listen_backlog;
  bootstrap_config.dispatch_timeout_ms = projected.settings->dispatch_timeout_ms;
  bootstrap_config.diag_enabled = projected.settings->diag_enabled;
  bootstrap_config.watchdog_enabled = projected.settings->watchdog_enabled;

  std::vector<DaemonConfigConflict> conflicts;
  if (request.deployment_config_path.has_value()) {
    const auto parsed_config = parse_deployment_config(*request.deployment_config_path);
    if (!parsed_config.ok()) {
      return DaemonEntryConfigLoadResult{
          .entry_config = std::nullopt,
          .message = "deployment snapshot parse failed: " + parsed_config.error,
      };
    }

    std::string overlay_error;
    if (!apply_deployment_overrides(parsed_config.values, &bootstrap_config,
                                    &overlay_error)) {
      return DaemonEntryConfigLoadResult{
          .entry_config = std::nullopt,
          .message = "deployment snapshot parse failed: " + overlay_error,
      };
    }

    const auto configured_socket_path =
        overlay_config_value(parsed_config.values, "daemon.socket_path");
    if (request.socket_path_override.has_value() && configured_socket_path.has_value() &&
        *request.socket_path_override != *configured_socket_path) {
      conflicts.push_back(DaemonConfigConflict{
          .key = "daemon.socket_path",
          .first_source = DaemonConfigSource::CommandLine,
          .second_source = DaemonConfigSource::ConfigFile,
          .first_value = *request.socket_path_override,
          .second_value = *configured_socket_path,
      });
    }
  }

  if (request.socket_path_override.has_value()) {
    bootstrap_config.socket_path = *request.socket_path_override;
  }

  DaemonEntryConfig entry_config;
  entry_config.bootstrap_config = std::move(bootstrap_config);
  entry_config.requested_profile_id = request.requested_profile_id;
  entry_config.effective_profile_id = runtime_snapshot.snapshot->effective_profile_id();
  entry_config.runtime_policy_snapshot = runtime_snapshot.snapshot;
  entry_config.config_revision = build_config_revision(
      runtime_snapshot.snapshot->effective_profile_id(),
      runtime_snapshot.snapshot->generation(),
      request.deployment_config_path);
  entry_config.conflicts = std::move(conflicts);

  if (!entry_config.has_consistent_values()) {
    return DaemonEntryConfigLoadResult{
        .entry_config = std::nullopt,
        .message = "daemon entry config produced an inconsistent process snapshot",
    };
  }

  return DaemonEntryConfigLoadResult{
      .entry_config = std::move(entry_config),
      .message = "daemon entry config loaded",
  };
}

}  // namespace dasall::apps::daemon