#include <exception>
#include <iostream>

#include "config/ConfigPlanFormatter.h"
#include "support/TestAssertions.h"

namespace {

dasall::apps::cli::config::ConfigActionPlan make_plan_fixture() {
  using dasall::apps::cli::config::ConfigActionPlan;
  using dasall::apps::cli::config::ConfigPlannedFileWrite;
  using dasall::apps::cli::config::ConfigPlannedSecretWrite;
  using dasall::apps::cli::config::InstallState;

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
  plan.blocked_actions.push_back("root_required_for_config_write");
  return plan;
}

void test_format_human_projects_states_file_writes_and_service_actions() {
  using dasall::apps::cli::config::ConfigPlanFormatter;
  using dasall::tests::support::assert_true;

  const auto human = ConfigPlanFormatter::format_human(make_plan_fixture());

  assert_true(human.find("BootstrapPending") != std::string::npos,
              "ConfigPlanFormatter should include state_before in human output");
  assert_true(human.find("ConfiguredRunning") != std::string::npos,
              "ConfigPlanFormatter should include state_after_expected in human output");
  assert_true(human.find("/etc/dasall/daemon.json") != std::string::npos,
              "ConfigPlanFormatter should include canonical file sinks in human output");
  assert_true(human.find("validate-only") != std::string::npos &&
                  human.find("restart") != std::string::npos &&
                  human.find("start") != std::string::npos,
              "ConfigPlanFormatter should summarize derived service actions in human output");
  assert_true(human.find("root_required_for_config_write") != std::string::npos,
              "ConfigPlanFormatter should surface blocked actions in human output");
}

void test_format_json_emits_stable_plan_schema_and_arrays() {
  using dasall::apps::cli::config::ConfigPlanFormatter;
  using dasall::tests::support::assert_true;

  const auto json = ConfigPlanFormatter::format_json(make_plan_fixture());

  assert_true(json.find("\"schema_version\":\"dasall.config.plan.v1\"") !=
                  std::string::npos,
              "ConfigPlanFormatter should emit the frozen plan schema version in JSON output");
  assert_true(json.find("\"path\":\"/etc/dasall/daemon.json\"") !=
                  std::string::npos,
              "ConfigPlanFormatter should include canonical file sinks in JSON output");
  assert_true(
      json.find("\"changed_keys\":[\"daemon.socket_path\",\"daemon.log_format\"]") !=
          std::string::npos,
      "ConfigPlanFormatter should emit stable changed_keys arrays in JSON output");
  assert_true(json.find("\"service_restart_required\":true") !=
                  std::string::npos,
              "ConfigPlanFormatter should include top-level service booleans in JSON output");
  assert_true(json.find("\"blocked_actions\":[\"root_required_for_config_write\"]") !=
                  std::string::npos,
              "ConfigPlanFormatter should include blocked_actions arrays in JSON output");
}

}  // namespace

int main() {
  try {
    test_format_human_projects_states_file_writes_and_service_actions();
    test_format_json_emits_stable_plan_schema_and_arrays();
  } catch (const std::exception& ex) {
    std::cerr << "ConfigPlanFormatterTest failed: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "ConfigPlanFormatterTest passed\n";
  return 0;
}