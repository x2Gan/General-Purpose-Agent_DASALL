#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config/ConfigCommandTypes.h"
#include "config/PrivilegeProbe.h"

namespace dasall::apps::cli::config {

struct ValidateOnlyResult {
  int exit_code = 0;
  std::string stdout_text;
  std::string stderr_text;
};

using ValidateOnlyRunner =
    std::function<ValidateOnlyResult(const std::vector<std::string>&)>;

struct ConfigPreflightEnvironment {
  std::filesystem::path daemon_binary = "/usr/bin/dasall-daemon";
  std::filesystem::path defaults_file = "/etc/default/dasall-daemon";
  std::filesystem::path daemon_config_file = "/etc/dasall/daemon.json";
  std::optional<bool> systemd_available;
  std::optional<bool> daemon_binary_available;
  std::optional<bool> defaults_file_writable;
  std::optional<bool> daemon_config_file_writable;
};

struct ConfigPreflightResult {
  bool ok = false;
  bool root_required = false;
  bool running_as_root = false;
  bool stdin_is_tty = false;
  bool systemd_available = false;
  bool daemon_validate_only_available = false;
  bool validate_only_passed = false;
  std::vector<std::string> validate_only_command;
  std::vector<std::string> manual_followups;
  std::vector<std::string> blocked_actions;
  std::vector<std::string> failure_reasons;

  [[nodiscard]] bool has_reason(std::string_view reason) const;
};

class ConfigPreflightChecker {
 public:
  explicit ConfigPreflightChecker(
      ConfigPreflightEnvironment environment = ConfigPreflightEnvironment{});

  [[nodiscard]] ConfigPreflightResult run(
      const ConfigActionPlan& plan,
      const DesiredConfigSnapshot& desired,
      const ValidateOnlyRunner& runner,
      std::optional<PrivilegeContext> privilege = std::nullopt) const;

 private:
  ConfigPreflightEnvironment environment_;
  PrivilegeProbe privilege_probe_;
};

}  // namespace dasall::apps::cli::config