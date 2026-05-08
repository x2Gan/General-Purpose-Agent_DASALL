#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "CliCommandParser.h"
#include "config/ConfigPreflightChecker.h"
#include "config/ConfigCommandTypes.h"
#include "config/ConfigDiffPlanner.h"
#include "config/DaemonConfigFileStore.h"
#include "config/InstallStateProbe.h"
#include "config/LlmSecretPage.h"
#include "config/ConfigSummaryFormatter.h"
#include "config/InteractivePromptEngine.h"
#include "config/PrivilegeProbe.h"
#include "config/ServiceManagerAdapter.h"
#include "secret/SecretBootstrapWriter.h"

namespace dasall::apps::cli::config {

enum class CliConfigWorkflowStatus {
  UnsupportedCommand,
  PendingImplementation,
  PlanRendered,
  SummaryRendered,
  ValidationRendered,
  ApplyRendered,
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
  ServiceCommandRunner service_command_runner;
  std::optional<PrivilegeContext> privilege_context;
  InteractivePromptEngine::InputHandler prompt_input_handler;
  InteractivePromptEngine::ConfirmHandler prompt_confirm_handler;
  LlmSecretPage::StdinReader secret_stdin_reader;
  std::filesystem::path secret_root_dir = "/var/lib/dasall/secrets";
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
  ConfigDiffPlanner diff_planner_;
  InstallStateProbe install_state_probe_;
  ServiceManagerAdapter service_manager_;
  InteractivePromptEngine prompt_engine_;
  dasall::infra::secret::SecretBootstrapWriter secret_bootstrap_writer_;
  LlmSecretPage llm_secret_page_;
};

}  // namespace dasall::apps::cli::config