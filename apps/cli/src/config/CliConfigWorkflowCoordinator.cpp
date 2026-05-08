#include "config/CliConfigWorkflowCoordinator.h"

#include <string_view>

#include "config/ConfigPlanFormatter.h"

namespace dasall::apps::cli::config {

namespace {

[[nodiscard]] std::string_view workflow_command_name(
    const dasall::apps::cli::CliConfigCommandKind command_kind) {
  using dasall::apps::cli::CliConfigCommandKind;

  switch (command_kind) {
    case CliConfigCommandKind::Wizard:
      return "config";
    case CliConfigCommandKind::Show:
      return "config.show";
    case CliConfigCommandKind::Plan:
      return "config.plan";
    case CliConfigCommandKind::Validate:
      return "config.validate";
    case CliConfigCommandKind::Apply:
      return "config.apply";
    case CliConfigCommandKind::None:
      return "config";
  }

  return "config";
}

[[nodiscard]] std::string pending_reason(
    const dasall::apps::cli::CliConfigCommandKind command_kind) {
  using dasall::apps::cli::CliConfigCommandKind;

  switch (command_kind) {
    case CliConfigCommandKind::Wizard:
      return "interactive config workflow skeleton is ready; desired-state collection is pending implementation";
    case CliConfigCommandKind::Show:
      return "config show workflow skeleton is ready; summary projection is pending implementation";
    case CliConfigCommandKind::Plan:
      return "config plan workflow skeleton is ready; diff planning is pending implementation";
    case CliConfigCommandKind::Validate:
      return "config validate workflow skeleton is ready; validate-only execution is pending implementation";
    case CliConfigCommandKind::Apply:
      return "config apply workflow skeleton is ready; apply executor is pending implementation";
    case CliConfigCommandKind::None:
      return "config workflow skeleton is ready; command dispatch is pending implementation";
  }

  return "config workflow skeleton is ready; command dispatch is pending implementation";
}

[[nodiscard]] std::string render_for_output_mode(
    const dasall::apps::cli::CliCommand& command,
    const std::string& human_output,
    const std::string& json_output) {
  return command.output_mode == dasall::apps::cli::CliOutputMode::Json
             ? json_output
             : human_output;
}

}  // namespace

CliConfigWorkflowResult CliConfigWorkflowCoordinator::run(
    const dasall::apps::cli::CliCommand& command) const {
  if (command.name != "config") {
    return CliConfigWorkflowResult{
        .handled = false,
        .success = false,
        .status = CliConfigWorkflowStatus::UnsupportedCommand,
        .command_name = {},
        .output = {},
        .failure_reason = "not a config workflow command",
    };
  }

  return CliConfigWorkflowResult{
      .handled = true,
      .success = false,
      .status = CliConfigWorkflowStatus::PendingImplementation,
      .command_name = std::string(workflow_command_name(command.config_command)),
      .output = {},
      .failure_reason = pending_reason(command.config_command),
  };
}

CliConfigWorkflowResult CliConfigWorkflowCoordinator::render_plan(
    const dasall::apps::cli::CliCommand& command,
    const ConfigActionPlan& plan) const {
  if (command.config_command != dasall::apps::cli::CliConfigCommandKind::Plan) {
    return CliConfigWorkflowResult{
        .handled = true,
        .success = false,
        .status = CliConfigWorkflowStatus::UnsupportedCommand,
        .command_name = std::string(workflow_command_name(command.config_command)),
        .output = {},
        .failure_reason = "plan rendering requires the config plan subcommand",
    };
  }

  return CliConfigWorkflowResult{
      .handled = true,
      .success = true,
      .status = CliConfigWorkflowStatus::PlanRendered,
      .command_name = std::string(workflow_command_name(command.config_command)),
      .output = render_for_output_mode(command,
                                       ConfigPlanFormatter::format_human(plan),
                                       ConfigPlanFormatter::format_json(plan)),
      .failure_reason = {},
  };
}

CliConfigWorkflowResult CliConfigWorkflowCoordinator::render_summary(
    const dasall::apps::cli::CliCommand& command,
    const ConfigSummaryView& summary) const {
  if (command.config_command != dasall::apps::cli::CliConfigCommandKind::Show) {
    return CliConfigWorkflowResult{
        .handled = true,
        .success = false,
        .status = CliConfigWorkflowStatus::UnsupportedCommand,
        .command_name = std::string(workflow_command_name(command.config_command)),
        .output = {},
        .failure_reason = "summary rendering requires the config show subcommand",
    };
  }

  return CliConfigWorkflowResult{
      .handled = true,
      .success = true,
      .status = CliConfigWorkflowStatus::SummaryRendered,
      .command_name = std::string(workflow_command_name(command.config_command)),
      .output = render_for_output_mode(
          command,
          ConfigSummaryFormatter::format_human(summary),
          ConfigSummaryFormatter::format_json(summary)),
      .failure_reason = {},
  };
}

}  // namespace dasall::apps::cli::config