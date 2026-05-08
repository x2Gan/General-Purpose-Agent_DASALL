#include "config/ServiceManagerAdapter.h"

#include <utility>

namespace dasall::apps::cli::config {
namespace {

constexpr std::string_view kBlockedReload = "service_reload";
constexpr std::string_view kBlockedRestart = "service_restart";
constexpr std::string_view kBlockedStart = "service_start";
constexpr std::string_view kBlockedEnable = "service_enable";
constexpr std::string_view kManualSystemdUnavailable =
    "systemd unavailable; automatic service actions must be performed manually";
constexpr std::string_view kManualStartDaemon =
    "start dasall-daemon manually after validate-only succeeds";
constexpr std::string_view kManualEnableDaemon =
    "configure service startup manually because systemd enable is unavailable";

void add_unique_string(std::vector<std::string>& values,
                       std::string_view value) {
  for (const auto& existing : values) {
    if (existing == value) {
      return;
    }
  }

  values.emplace_back(value);
}

[[nodiscard]] ServiceManagerCommand make_systemctl_command(
    const ServiceActionKind action,
    const ServiceManagerAdapterOptions& options) {
  std::string verb;
  switch (action) {
    case ServiceActionKind::Reload:
      verb = "reload";
      break;
    case ServiceActionKind::Restart:
      verb = "restart";
      break;
    case ServiceActionKind::Start:
      verb = "start";
      break;
    case ServiceActionKind::Enable:
      verb = "enable";
      break;
  }

  return ServiceManagerCommand{
      .action = action,
      .argv = {options.systemctl_path, std::move(verb), options.daemon_unit},
  };
}

}  // namespace

std::string_view to_string(const ServiceActionKind action) {
  switch (action) {
    case ServiceActionKind::Reload:
      return "reload";
    case ServiceActionKind::Restart:
      return "restart";
    case ServiceActionKind::Start:
      return "start";
    case ServiceActionKind::Enable:
      return "enable";
  }

  return "start";
}

bool ServiceExecutionPlan::empty() const {
  return commands.empty() && manual_followups.empty() && blocked_actions.empty();
}

ServiceManagerAdapter::ServiceManagerAdapter(ServiceManagerAdapterOptions options)
    : options_(std::move(options)) {}

ServiceExecutionPlan ServiceManagerAdapter::plan_service_actions(
    const ConfigActionPlan& plan,
    const bool systemd_available) const {
  ServiceExecutionPlan execution_plan;

  const bool needs_reload =
      plan.service_reload_required && !plan.service_restart_required;
  const bool needs_restart = plan.service_restart_required;
  const bool needs_start = plan.service_start_requested && !needs_restart;
  const bool needs_enable = plan.service_enable_requested;

  if (!systemd_available) {
    if (needs_reload) {
      add_unique_string(execution_plan.blocked_actions, kBlockedReload);
    }
    if (needs_restart) {
      add_unique_string(execution_plan.blocked_actions, kBlockedRestart);
    }
    if (needs_start) {
      add_unique_string(execution_plan.blocked_actions, kBlockedStart);
    }
    if (needs_enable) {
      add_unique_string(execution_plan.blocked_actions, kBlockedEnable);
    }

    if (!execution_plan.blocked_actions.empty()) {
      add_unique_string(execution_plan.manual_followups,
                        kManualSystemdUnavailable);
    }
    if (needs_reload || needs_restart || needs_start) {
      add_unique_string(execution_plan.manual_followups, kManualStartDaemon);
    }
    if (needs_enable) {
      add_unique_string(execution_plan.manual_followups, kManualEnableDaemon);
    }

    return execution_plan;
  }

  if (needs_reload) {
    execution_plan.commands.push_back(
        make_systemctl_command(ServiceActionKind::Reload, options_));
  }
  if (needs_restart) {
    execution_plan.commands.push_back(
        make_systemctl_command(ServiceActionKind::Restart, options_));
  } else if (needs_start) {
    execution_plan.commands.push_back(
        make_systemctl_command(ServiceActionKind::Start, options_));
  }
  if (needs_enable) {
    execution_plan.commands.push_back(
        make_systemctl_command(ServiceActionKind::Enable, options_));
  }

  return execution_plan;
}

ServiceApplyResult ServiceManagerAdapter::apply(
    const ServiceExecutionPlan& plan,
    const ServiceCommandRunner& runner) const {
  ServiceApplyResult result;
  result.degraded = !plan.blocked_actions.empty();
  result.manual_followups = plan.manual_followups;
  result.blocked_actions = plan.blocked_actions;

  if (!runner && !plan.commands.empty()) {
    result.error_message = "service command runner is not available";
    return result;
  }

  for (const auto& command : plan.commands) {
    const auto command_result = runner(command);
    if (command_result.exit_code != 0) {
      result.error_message =
          "service action " + std::string(to_string(command.action)) +
          " failed";
      if (!command_result.stderr_text.empty()) {
        result.error_message += ": " + command_result.stderr_text;
      } else if (!command_result.stdout_text.empty()) {
        result.error_message += ": " + command_result.stdout_text;
      }
      return result;
    }

    result.completed_actions.emplace_back(to_string(command.action));
  }

  result.success = true;
  return result;
}

}  // namespace dasall::apps::cli::config