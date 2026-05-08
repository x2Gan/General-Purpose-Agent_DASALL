#include "config/ConfigDiffPlanner.h"

#include <fstream>
#include <sstream>
#include <string_view>

#include "config/DaemonConfigFileStore.h"

namespace dasall::apps::cli::config {

namespace {

enum class DesiredConfigSection {
  Root,
  Daemon,
  Service,
  OperatorAccess,
  Secrets,
};

void assign_error(std::string* error_message, std::string message) {
  if (error_message != nullptr) {
    *error_message = std::move(message);
  }
}

[[nodiscard]] std::string trim_copy(std::string_view text) {
  std::size_t begin = 0;
  while (begin < text.size() &&
         std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }

  std::size_t end = text.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(text[end - 1U])) != 0) {
    --end;
  }

  return std::string(text.substr(begin, end - begin));
}

[[nodiscard]] bool read_text_file(const std::filesystem::path& path,
                                  std::string* content,
                                  std::string* error_message) {
  std::ifstream stream(path);
  if (!stream.is_open()) {
    assign_error(error_message, "unable to open desired-state file: " +
                                    path.string());
    return false;
  }

  std::ostringstream buffer;
  buffer << stream.rdbuf();
  *content = buffer.str();
  return true;
}

[[nodiscard]] bool parse_bool_value(std::string_view value, bool* output) {
  if (value == "true") {
    *output = true;
    return true;
  }
  if (value == "false") {
    *output = false;
    return true;
  }
  return false;
}

[[nodiscard]] std::string unquote_copy(std::string_view value) {
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    return std::string(value.substr(1, value.size() - 2));
  }
  return std::string(value);
}

[[nodiscard]] bool parse_inline_empty_list(std::string_view value) {
  return trim_copy(value) == "[]";
}

[[nodiscard]] bool restart_required_for_daemon_key(std::string_view key) {
  return key == "daemon.socket_path" || key == "daemon.log_format" ||
         key == "daemon.diag_enabled" || key == "daemon.override_enabled" ||
         key == "daemon.watchdog_enabled";
}

[[nodiscard]] InstallState expected_state_after_apply(
    const InstallState state_before,
    const DesiredServiceSettings& service_settings) {
  if (state_before == InstallState::Unsupported) {
    return InstallState::Unsupported;
  }

  if (service_settings.start_now) {
    return InstallState::ConfiguredRunning;
  }

  if (state_before == InstallState::ConfiguredRunning) {
    return InstallState::ConfiguredRunning;
  }

  return InstallState::ConfiguredStopped;
}

}  // namespace

std::optional<DesiredConfigSnapshot> ConfigDiffPlanner::load_desired_from_file(
    const std::filesystem::path& path,
    std::string* error_message) const {
  std::string content;
  if (!read_text_file(path, &content, error_message)) {
    return std::nullopt;
  }

  DesiredConfigSnapshot desired;
  DesiredConfigSection section = DesiredConfigSection::Root;
  std::istringstream stream(content);
  std::string raw_line;
  std::size_t line_number = 0;
  while (std::getline(stream, raw_line)) {
    ++line_number;
    if (!raw_line.empty() && raw_line.back() == '\r') {
      raw_line.pop_back();
    }
    const std::string trimmed = trim_copy(raw_line);
    if (trimmed.empty() || trimmed.starts_with('#')) {
      continue;
    }

    const std::size_t indent = raw_line.find_first_not_of(' ');
    if (indent == std::string::npos) {
      continue;
    }

    if (trimmed.starts_with('-')) {
      assign_error(error_message,
                   "desired-state lists beyond inline [] are not supported yet: line " +
                       std::to_string(line_number));
      return std::nullopt;
    }

    const auto delimiter = trimmed.find(':');
    if (delimiter == std::string::npos) {
      assign_error(error_message,
                   "invalid desired-state line, expected key: value: line " +
                       std::to_string(line_number));
      return std::nullopt;
    }

    const std::string key = trim_copy(trimmed.substr(0, delimiter));
    const std::string value = trim_copy(trimmed.substr(delimiter + 1U));

    if (indent == 0) {
      if (value.empty()) {
        if (key == "daemon") {
          section = DesiredConfigSection::Daemon;
          continue;
        }
        if (key == "service") {
          section = DesiredConfigSection::Service;
          continue;
        }
        if (key == "operator_access") {
          section = DesiredConfigSection::OperatorAccess;
          continue;
        }
        if (key == "secrets") {
          section = DesiredConfigSection::Secrets;
          continue;
        }
      }

      section = DesiredConfigSection::Root;
      if (key == "schema_version") {
        desired.schema_version = unquote_copy(value);
        continue;
      }
      if (key == "profile_id") {
        desired.profile_id = unquote_copy(value);
        continue;
      }

      assign_error(error_message,
                   "unsupported desired-state root key '" + key +
                       "': line " + std::to_string(line_number));
      return std::nullopt;
    }

    if (indent != 2) {
      assign_error(error_message,
                   "unsupported desired-state indentation: line " +
                       std::to_string(line_number));
      return std::nullopt;
    }

    switch (section) {
      case DesiredConfigSection::Daemon:
        if (key == "socket_path") {
          desired.daemon.socket_path = unquote_copy(value);
          continue;
        }
        if (key == "log_format") {
          desired.daemon.log_format = unquote_copy(value);
          continue;
        }
        if (key == "diag_enabled") {
          if (!parse_bool_value(value, &desired.daemon.diag_enabled)) {
            assign_error(error_message,
                         "invalid daemon.diag_enabled boolean: line " +
                             std::to_string(line_number));
            return std::nullopt;
          }
          continue;
        }
        if (key == "override_enabled") {
          if (!parse_bool_value(value, &desired.daemon.override_enabled)) {
            assign_error(error_message,
                         "invalid daemon.override_enabled boolean: line " +
                             std::to_string(line_number));
            return std::nullopt;
          }
          continue;
        }
        if (key == "watchdog_enabled") {
          if (!parse_bool_value(value, &desired.daemon.watchdog_enabled)) {
            assign_error(error_message,
                         "invalid daemon.watchdog_enabled boolean: line " +
                             std::to_string(line_number));
            return std::nullopt;
          }
          continue;
        }
        break;
      case DesiredConfigSection::Service:
        if (key == "start_now") {
          if (!parse_bool_value(value, &desired.service.start_now)) {
            assign_error(error_message,
                         "invalid service.start_now boolean: line " +
                             std::to_string(line_number));
            return std::nullopt;
          }
          continue;
        }
        if (key == "enable_on_boot") {
          if (!parse_bool_value(value, &desired.service.enable_on_boot)) {
            assign_error(error_message,
                         "invalid service.enable_on_boot boolean: line " +
                             std::to_string(line_number));
            return std::nullopt;
          }
          continue;
        }
        break;
      case DesiredConfigSection::OperatorAccess:
        if (key == "add_users" && parse_inline_empty_list(value)) {
          desired.operator_access.add_users.clear();
          continue;
        }
        break;
      case DesiredConfigSection::Secrets:
        if (key == "refs" && parse_inline_empty_list(value)) {
          desired.secrets.refs.clear();
          continue;
        }
        break;
      case DesiredConfigSection::Root:
        break;
    }

    assign_error(error_message,
                 "unsupported desired-state key '" + key + "' in section: line " +
                     std::to_string(line_number));
    return std::nullopt;
  }

  if (desired.schema_version != kDesiredConfigSchemaVersion) {
    assign_error(error_message,
                 "unsupported desired-state schema_version: " +
                     desired.schema_version);
    return std::nullopt;
  }
  if (!desired.is_well_formed()) {
    assign_error(error_message, "desired-state file is not well formed");
    return std::nullopt;
  }
  return desired;
}

ConfigActionPlan ConfigDiffPlanner::build_plan(
    const DesiredConfigSnapshot& current,
    const DesiredConfigSnapshot& desired,
    const InstallState state_before,
    const DaemonConfigFileStorePaths& store_paths) const {
  ConfigActionPlan plan;
  plan.state_before = state_before;
  plan.state_after_expected =
      expected_state_after_apply(state_before, desired.service);

  if (current.profile_id != desired.profile_id) {
    plan.file_writes.push_back(ConfigPlannedFileWrite{
        .path = store_paths.defaults_file.string(),
        .operation = std::filesystem::exists(store_paths.defaults_file) ? "update"
                                                                        : "create",
        .requires_root = true,
        .restart_required = true,
        .changed_keys = {"profile_id"},
    });
  }

  std::vector<std::string> daemon_changed_keys;
  if (current.daemon.socket_path != desired.daemon.socket_path) {
    daemon_changed_keys.emplace_back("daemon.socket_path");
  }
  if (current.daemon.log_format != desired.daemon.log_format) {
    daemon_changed_keys.emplace_back("daemon.log_format");
  }
  if (current.daemon.diag_enabled != desired.daemon.diag_enabled) {
    daemon_changed_keys.emplace_back("daemon.diag_enabled");
  }
  if (current.daemon.override_enabled != desired.daemon.override_enabled) {
    daemon_changed_keys.emplace_back("daemon.override_enabled");
  }
  if (current.daemon.watchdog_enabled != desired.daemon.watchdog_enabled) {
    daemon_changed_keys.emplace_back("daemon.watchdog_enabled");
  }

  if (!daemon_changed_keys.empty()) {
    bool restart_required = false;
    for (const auto& key : daemon_changed_keys) {
      restart_required = restart_required || restart_required_for_daemon_key(key);
    }

    plan.file_writes.push_back(ConfigPlannedFileWrite{
        .path = store_paths.daemon_config_file.string(),
        .operation = std::filesystem::exists(store_paths.daemon_config_file)
                         ? "update"
                         : "create",
        .requires_root = true,
        .restart_required = restart_required,
        .changed_keys = daemon_changed_keys,
    });
  }

  for (const auto& secret_ref : desired.secrets.refs) {
    plan.secret_writes.push_back(ConfigPlannedSecretWrite{
        .ref = secret_ref.ref,
        .operation = "create_or_rotate",
        .runtime_verification = kConfigRuntimeVerificationPending,
    });
  }

  plan.service_validate_requested = !plan.file_writes.empty() ||
                                    desired.service.start_now ||
                                    desired.service.enable_on_boot;
  plan.service_restart_required = false;
  for (const auto& file_write : plan.file_writes) {
    plan.service_restart_required = plan.service_restart_required ||
                                    file_write.restart_required;
  }
  plan.service_start_requested = desired.service.start_now;
  plan.service_enable_requested = desired.service.enable_on_boot;

  if (!desired.operator_access.add_users.empty()) {
    plan.blocked_actions.emplace_back("operator_access_apply_not_supported");
    plan.manual_followups.emplace_back(
        "apply operator access changes manually until CLCFG-TODO-015 lands");
  }
  if (!desired.secrets.refs.empty()) {
    plan.blocked_actions.emplace_back("secret_bootstrap_apply_not_supported");
    plan.manual_followups.emplace_back(
        "provision secrets through the future bootstrap writer from CLCFG-TODO-016");
  }

  return plan;
}

}  // namespace dasall::apps::cli::config