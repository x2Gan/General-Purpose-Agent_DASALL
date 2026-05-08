#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "config/ConfigCommandTypes.h"

namespace dasall::apps::cli::config {

enum class ServiceActionKind {
  Reload,
  Restart,
  Start,
  Enable,
};

[[nodiscard]] std::string_view to_string(ServiceActionKind action);

struct ServiceManagerCommand {
  ServiceActionKind action = ServiceActionKind::Start;
  std::vector<std::string> argv;
};

struct ServiceExecutionPlan {
  std::vector<ServiceManagerCommand> commands;
  std::vector<std::string> manual_followups;
  std::vector<std::string> blocked_actions;

  [[nodiscard]] bool empty() const;
};

struct ServiceCommandResult {
  int exit_code = 0;
  std::string stdout_text;
  std::string stderr_text;
};

using ServiceCommandRunner =
    std::function<ServiceCommandResult(const ServiceManagerCommand&)>;

struct ServiceApplyResult {
  bool success = false;
  bool degraded = false;
  std::vector<std::string> completed_actions;
  std::vector<std::string> manual_followups;
  std::vector<std::string> blocked_actions;
  std::string error_message;
};

struct ServiceManagerAdapterOptions {
  std::string systemctl_path = "systemctl";
  std::string daemon_unit = "dasall-daemon.service";
};

class ServiceManagerAdapter {
 public:
  explicit ServiceManagerAdapter(
      ServiceManagerAdapterOptions options = ServiceManagerAdapterOptions{});

  [[nodiscard]] ServiceExecutionPlan plan_service_actions(
      const ConfigActionPlan& plan,
      bool systemd_available) const;

  [[nodiscard]] ServiceApplyResult apply(
      const ServiceExecutionPlan& plan,
      const ServiceCommandRunner& runner) const;

 private:
  ServiceManagerAdapterOptions options_;
};

}  // namespace dasall::apps::cli::config