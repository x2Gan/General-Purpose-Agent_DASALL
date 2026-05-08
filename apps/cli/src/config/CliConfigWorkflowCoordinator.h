#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "CliCommandParser.h"
#include "config/ConfigPreflightChecker.h"
#include "config/ConfigCommandTypes.h"
#include "config/DaemonConfigFileStore.h"
#include "config/InstallStateProbe.h"
#include "config/ConfigSummaryFormatter.h"

namespace dasall::apps::cli::config {

enum class CliConfigWorkflowStatus {
  UnsupportedCommand,
  PendingImplementation,
  PlanRendered,
  SummaryRendered,
  ValidationRendered,
  WorkflowFailed,
};

struct CliConfigWorkflowResult {
  bool handled = false;
  bool success = false;
  CliConfigWorkflowStatus status = CliConfigWorkflowStatus::UnsupportedCommand;
  int exit_code = 7;
  std::string command_name;
  std::string output;
  std::string failure_reason;
};

struct CliConfigWorkflowDependencies {
  DaemonConfigFileStorePaths store_paths;
  ConfigPreflightEnvironment preflight_environment;
  ValidateOnlyRunner validate_only_runner;
};

class CliConfigWorkflowCoordinator {
 public:
  explicit CliConfigWorkflowCoordinator(
      CliConfigWorkflowDependencies dependencies = CliConfigWorkflowDependencies{});

  [[nodiscard]] CliConfigWorkflowResult run(
      const dasall::apps::cli::CliCommand& command) const;

  [[nodiscard]] CliConfigWorkflowResult render_plan(
      const dasall::apps::cli::CliCommand& command,
      const ConfigActionPlan& plan) const;

  [[nodiscard]] CliConfigWorkflowResult render_summary(
      const dasall::apps::cli::CliCommand& command,
      const ConfigSummaryView& summary) const;

  [[nodiscard]] CliConfigWorkflowResult render_validation(
      const dasall::apps::cli::CliCommand& command,
      bool success,
      InstallState state_before,
      const ConfigPreflightResult& preflight_result) const;

 private:
  CliConfigWorkflowDependencies dependencies_;
  DaemonConfigFileStore file_store_;
  ConfigPreflightChecker preflight_checker_;
  InstallStateProbe install_state_probe_;
};

}  // namespace dasall::apps::cli::config