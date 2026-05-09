#include <exception>
#include <iostream>
#include <string>

#include "CliCommandParser.h"
#include "config/CliConfigWorkflowCoordinator.h"
#include "config/ConfigSummaryFormatter.h"
#include "config/ToolSkillPage.h"
#include "support/TestAssertions.h"

namespace {

using dasall::apps::cli::CliCommand;
using dasall::apps::cli::CliConfigCommandKind;
using dasall::apps::cli::CliOutputMode;
using dasall::apps::cli::config::CliConfigWorkflowCoordinator;
using dasall::apps::cli::config::CliConfigWorkflowStatus;
using dasall::apps::cli::config::ConfigActionPlan;
using dasall::apps::cli::config::ConfigApplyResult;
using dasall::apps::cli::config::ConfigPlannedFileWrite;
using dasall::apps::cli::config::ConfigPlannedSecretWrite;
using dasall::apps::cli::config::ConfigPreflightResult;
using dasall::apps::cli::config::ConfigSecretSummaryEntry;
using dasall::apps::cli::config::ConfigSummaryView;
using dasall::apps::cli::config::InstallState;
using dasall::apps::cli::config::ToolSkillPageMode;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] CliCommand make_command(
    const CliConfigCommandKind command_kind,
    const CliOutputMode output_mode = CliOutputMode::Human) {
  CliCommand command;
  command.name = "config";
  command.config_command = command_kind;
  command.output_mode = output_mode;
  return command;
}

[[nodiscard]] ConfigActionPlan make_plan_fixture() {
  ConfigActionPlan plan;
  plan.state_before = InstallState::BootstrapPending;
  plan.state_after_expected = InstallState::ConfiguredRunning;
  plan.file_writes.push_back(ConfigPlannedFileWrite{
      .path = "/etc/dasall/daemon.json",
      .operation = "update",
      .requires_root = true,
      .restart_required = true,
      .changed_keys = {"daemon.socket_path", "daemon.log_format"},
  });
  plan.secret_writes.push_back(ConfigPlannedSecretWrite{
      .ref = "secret://llm/providers/deepseek-prod",
      .operation = "create_or_rotate",
      .runtime_verification = "pending",
  });
  plan.service_validate_requested = true;
  plan.service_restart_required = true;
  plan.service_start_requested = true;
  plan.manual_followups.push_back("systemctl status dasall-daemon.service");
  return plan;
}

[[nodiscard]] ConfigSummaryView make_summary_fixture() {
  ConfigSummaryView summary;
  summary.profile_id = "desktop_full";
  summary.socket_path = "/run/dasall/daemon.sock";
  summary.log_format = "json";
  summary.tool_skill_page.mode = ToolSkillPageMode::SummaryOnly;
  summary.tool_skill_page.controls_enabled = false;
  summary.tool_skill_page.banner = "read-only summary";
  summary.tool_skill_page.summary_items = {"active_tooling_detected: true",
                                           "controls: read-only"};
  summary.tool_skill_page.constraints = {
      "source-scoped revoke path required",
  };
  summary.secret_refs.push_back(ConfigSecretSummaryEntry{
      .ref = "secret://llm/providers/deepseek-prod",
      .status = "configured",
  });
  summary.service_installed = true;
  summary.service_running = true;
  summary.service_enabled = false;
  summary.ping_status = "ready";
  summary.readiness_status = "ready";
  summary.operator_access_hint =
      "use sudo dasall config to modify install-state files";
  summary.next_steps.push_back("systemctl enable --now dasall-daemon.service");
  summary.apply_result = ConfigApplyResult{
      .outcome = "applied",
      .state_before = InstallState::BootstrapPending,
      .state_after = InstallState::ConfiguredRunning,
      .applied = true,
      .rollback_performed = false,
      .written_files = {"/etc/dasall/daemon.json"},
      .written_secret_refs = {"secret://llm/providers/deepseek-prod"},
      .completed_actions = {"restart", "enable"},
      .manual_followups = {},
      .blocked_actions = {},
  };
  return summary;
}

[[nodiscard]] ConfigPreflightResult make_validate_failure_fixture() {
  ConfigPreflightResult result;
  result.ok = false;
  result.running_as_root = true;
  result.stdin_is_tty = false;
  result.systemd_available = true;
  result.daemon_validate_only_available = true;
  result.validate_only_passed = false;
  result.validate_only_command = {
      "/usr/bin/dasall-daemon",
      "--validate-only",
      "--config-file",
      "/etc/dasall/daemon.json",
      "--profile-id",
      "desktop_full",
  };
  result.failure_reasons = {"daemon_validate_only_failed"};
  result.manual_followups = {"inspect /etc/dasall/daemon.json"};
  return result;
}

void test_plan_output_contract_keeps_stable_human_and_json_projection() {
  CliConfigWorkflowCoordinator coordinator;

  const auto human_result = coordinator.render_plan(
      make_command(CliConfigCommandKind::Plan), make_plan_fixture());
  assert_true(human_result.handled && human_result.success,
              "ConfigOutputContractTest should keep plan rendering on the success path");
  assert_true(human_result.status == CliConfigWorkflowStatus::PlanRendered,
              "config plan contract should keep the stable plan-rendered status");
  assert_equal(0,
               human_result.exit_code,
               "config plan contract should keep exit code 0 on success");
  assert_equal(std::string("config.plan"),
               human_result.command_name,
               "config plan contract should keep the stable command_name projection");
  assert_true(human_result.output.find("[dasall-config] plan") !=
                      std::string::npos &&
                  human_result.output.find("service_actions: validate-only, restart, start") !=
                      std::string::npos,
              "config plan contract should keep the human projection banner and ordered service actions");

  const auto json_result = coordinator.render_plan(
      make_command(CliConfigCommandKind::Plan, CliOutputMode::Json),
      make_plan_fixture());
  assert_true(json_result.output.find("\"schema_version\":\"dasall.config.plan.v1\"") !=
                      std::string::npos &&
                  json_result.output.find("\"state_before\":\"BootstrapPending\"") !=
                      std::string::npos &&
                  json_result.output.find("\"path\":\"/etc/dasall/daemon.json\"") !=
                      std::string::npos,
              "config plan contract should keep the frozen JSON schema, state and canonical sink keys");
}

void test_summary_output_contract_keeps_redacted_refs_and_summary_schema() {
  CliConfigWorkflowCoordinator coordinator;

  const auto human_result = coordinator.render_summary(
      make_command(CliConfigCommandKind::Show), make_summary_fixture());
  assert_true(human_result.handled && human_result.success,
              "ConfigOutputContractTest should keep summary rendering on the success path");
  assert_true(human_result.status == CliConfigWorkflowStatus::SummaryRendered,
              "config show contract should keep the stable summary-rendered status");
  assert_equal(0,
               human_result.exit_code,
               "config show contract should keep exit code 0 on success");
  assert_equal(std::string("config.show"),
               human_result.command_name,
               "config show contract should keep the stable command_name projection");
  assert_true(human_result.output.find("[dasall-config] summary") !=
                      std::string::npos &&
                  human_result.output.find("tool_skill.mode: summary_only") !=
                      std::string::npos &&
                  human_result.output.find(
                      "secret://llm/providers/deepseek-prod (configured)") !=
                      std::string::npos,
              "config show contract should keep human summary projection and redacted secret refs");

  const auto json_result = coordinator.render_summary(
      make_command(CliConfigCommandKind::Show, CliOutputMode::Json),
      make_summary_fixture());
  assert_true(json_result.output.find("\"schema_version\":\"dasall.config.summary.v1\"") !=
                      std::string::npos &&
                  json_result.output.find("\"operator_access_hint\":\"use sudo dasall config to modify install-state files\"") !=
                      std::string::npos &&
                  json_result.output.find("\"tool_skill\":{\"mode\":\"summary_only\"") !=
                      std::string::npos,
              "config show contract should keep the frozen JSON summary schema and operator access projection");
}

void test_validation_output_contract_keeps_failure_schema_and_exit_family() {
  CliConfigWorkflowCoordinator coordinator;

  const auto human_result = coordinator.render_validation(
      make_command(CliConfigCommandKind::Validate),
      false,
      InstallState::Drifted,
      make_validate_failure_fixture());
  assert_true(human_result.handled && !human_result.success,
              "ConfigOutputContractTest should keep validation failures on the failure path");
  assert_true(human_result.status == CliConfigWorkflowStatus::ValidationRendered,
              "config validate contract should keep the stable validation-rendered status");
  assert_equal(5,
               human_result.exit_code,
               "config validate contract should map deterministic validate-only failures to exit code 5");
  assert_equal(std::string("config.validate"),
               human_result.command_name,
               "config validate contract should keep the stable command_name projection");
  assert_true(human_result.output.find("validation: failed") != std::string::npos &&
                  human_result.output.find("daemon_validate_only_failed") !=
                      std::string::npos,
              "config validate contract should keep the human failure summary and stable failure reason");

  const auto json_result = coordinator.render_validation(
      make_command(CliConfigCommandKind::Validate, CliOutputMode::Json),
      false,
      InstallState::Drifted,
      make_validate_failure_fixture());
  assert_true(json_result.output.find("\"schema_version\":\"dasall.config.validate.v1\"") !=
                      std::string::npos &&
                  json_result.output.find("\"state_before\":\"Drifted\"") !=
                      std::string::npos &&
                  json_result.output.find("\"failure_reasons\":[\"daemon_validate_only_failed\"]") !=
                      std::string::npos,
              "config validate contract should keep the frozen JSON schema and failure reasons array");
}

}  // namespace

int main() {
  try {
    test_plan_output_contract_keeps_stable_human_and_json_projection();
    test_summary_output_contract_keeps_redacted_refs_and_summary_schema();
    test_validation_output_contract_keeps_failure_schema_and_exit_family();
  } catch (const std::exception& ex) {
    std::cerr << "ConfigOutputContractTest failed: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "ConfigOutputContractTest passed\n";
  return 0;
}