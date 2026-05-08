#include "config/ConfigPreflightChecker.h"

#include <unistd.h>

#include <filesystem>
#include <utility>

#include "daemon/DaemonEndpointDefaults.h"

namespace dasall::apps::cli::config {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kReasonDaemonBinaryUnavailable =
    "daemon_binary_unavailable";
constexpr std::string_view kReasonDaemonValidateOnlyUnavailable =
    "daemon_validate_only_unavailable";
constexpr std::string_view kReasonDaemonValidateOnlyFailed =
    "daemon_validate_only_failed";
constexpr std::string_view kReasonDefaultsNotWritable =
    "canonical_defaults_not_writable";
constexpr std::string_view kReasonDaemonConfigNotWritable =
    "canonical_daemon_config_not_writable";
constexpr std::string_view kReasonNonCanonicalSocketPath =
    "non_canonical_socket_path";

void add_unique_string(std::vector<std::string>& values,
                       std::string_view value) {
  for (const auto& existing : values) {
    if (existing == value) {
      return;
    }
  }

  values.emplace_back(value);
}

[[nodiscard]] bool path_exists_and_executable(const fs::path& path) {
  return ::access(path.c_str(), X_OK) == 0;
}

[[nodiscard]] bool path_writable_or_parent_writable(const fs::path& path) {
  if (::access(path.c_str(), W_OK) == 0) {
    return true;
  }

  const auto parent = path.parent_path();
  return !parent.empty() && ::access(parent.c_str(), W_OK) == 0;
}

[[nodiscard]] bool systemd_runtime_available() {
  std::error_code error;
  return fs::exists("/run/systemd/system", error) &&
         fs::is_directory("/run/systemd/system", error);
}

[[nodiscard]] bool planned_write_targets_writable(
    const ConfigActionPlan& plan,
    const fs::path& target_path,
    const bool writable) {
  if (!writable) {
    for (const auto& file_write : plan.file_writes) {
      if (fs::path(file_write.path) == target_path) {
        return false;
      }
    }
  }

  return true;
}

}  // namespace

bool ConfigPreflightResult::has_reason(const std::string_view reason) const {
  for (const auto& existing : failure_reasons) {
    if (existing == reason) {
      return true;
    }
  }

  return false;
}

ConfigPreflightChecker::ConfigPreflightChecker(
    ConfigPreflightEnvironment environment)
    : environment_(std::move(environment)) {}

ConfigPreflightResult ConfigPreflightChecker::run(
    const ConfigActionPlan& plan,
    const DesiredConfigSnapshot& desired,
    const ValidateOnlyRunner& runner,
    std::optional<PrivilegeContext> privilege) const {
  ConfigPreflightResult result;

  const auto privilege_context = privilege.value_or(privilege_probe_.current());
  result.running_as_root = privilege_context.running_as_root;
  result.stdin_is_tty = privilege_context.stdin_is_tty;
  result.systemd_available = environment_.systemd_available.value_or(
      systemd_runtime_available());

  const auto binary_available = environment_.daemon_binary_available.value_or(
      path_exists_and_executable(environment_.daemon_binary));
  const auto defaults_writable = environment_.defaults_file_writable.value_or(
      path_writable_or_parent_writable(environment_.defaults_file));
  const auto daemon_config_writable =
      environment_.daemon_config_file_writable.value_or(
          path_writable_or_parent_writable(environment_.daemon_config_file));

  const auto privilege_requirement =
      privilege_probe_.require_root_for_write(plan, privilege_context);
  result.root_required = privilege_requirement.root_required;
  for (const auto& reason : privilege_requirement.failure_reasons) {
    add_unique_string(result.failure_reasons, reason);
  }

  if (!binary_available) {
    add_unique_string(result.failure_reasons, kReasonDaemonBinaryUnavailable);
  }

  if (!planned_write_targets_writable(plan, environment_.defaults_file,
                                      defaults_writable)) {
    add_unique_string(result.failure_reasons, kReasonDefaultsNotWritable);
  }
  if (!planned_write_targets_writable(plan, environment_.daemon_config_file,
                                      daemon_config_writable)) {
    add_unique_string(result.failure_reasons, kReasonDaemonConfigNotWritable);
  }

  if (desired.daemon.socket_path !=
      dasall::access::daemon::kDefaultDaemonSocketPath) {
    add_unique_string(result.failure_reasons, kReasonNonCanonicalSocketPath);
  }

  result.daemon_validate_only_available = binary_available &&
                                          static_cast<bool>(runner);
  if (plan.service_validate_requested) {
    if (!result.daemon_validate_only_available) {
      add_unique_string(result.failure_reasons,
                        kReasonDaemonValidateOnlyUnavailable);
    } else {
      result.validate_only_command = {
          environment_.daemon_binary.string(),
          "--validate-only",
          "--config-file",
          environment_.daemon_config_file.string(),
      };
      if (!desired.profile_id.empty()) {
        result.validate_only_command.push_back("--profile-id");
        result.validate_only_command.push_back(desired.profile_id);
      }

      const auto validate_result = runner(result.validate_only_command);
      result.validate_only_passed = validate_result.exit_code == 0;
      if (!result.validate_only_passed) {
        add_unique_string(result.failure_reasons,
                          kReasonDaemonValidateOnlyFailed);
      }
    }
  }

  result.ok = result.failure_reasons.empty();
  return result;
}

}  // namespace dasall::apps::cli::config