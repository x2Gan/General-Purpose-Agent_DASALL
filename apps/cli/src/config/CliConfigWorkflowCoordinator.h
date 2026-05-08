#pragma once

#include <string>

#include "CliCommandParser.h"
#include "config/ConfigCommandTypes.h"
#include "config/ConfigSummaryFormatter.h"

namespace dasall::apps::cli::config {

enum class CliConfigWorkflowStatus {
  UnsupportedCommand,
  PendingImplementation,
  PlanRendered,
  SummaryRendered,
};

struct CliConfigWorkflowResult {
  bool handled = false;
  bool success = false;
  CliConfigWorkflowStatus status = CliConfigWorkflowStatus::UnsupportedCommand;
  std::string command_name;
  std::string output;
  std::string failure_reason;
};

class CliConfigWorkflowCoordinator {
 public:
  [[nodiscard]] CliConfigWorkflowResult run(
      const dasall::apps::cli::CliCommand& command) const;

  [[nodiscard]] CliConfigWorkflowResult render_plan(
      const dasall::apps::cli::CliCommand& command,
      const ConfigActionPlan& plan) const;

  [[nodiscard]] CliConfigWorkflowResult render_summary(
      const dasall::apps::cli::CliCommand& command,
      const ConfigSummaryView& summary) const;
};

}  // namespace dasall::apps::cli::config