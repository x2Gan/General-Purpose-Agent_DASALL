#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

#include <unistd.h>

#include "config/CliConfigWorkflowCoordinator.h"
#include "support/TestAssertions.h"

namespace {

namespace fs = std::filesystem;

[[nodiscard]] fs::path make_temp_directory(std::string_view stem) {
  const auto unique_id = std::to_string(::getpid()) + "-" +
                         std::to_string(
                             fs::file_time_type::clock::now().time_since_epoch().count());
  const fs::path temp_root = fs::temp_directory_path() /
                             (std::string(stem) + "-" + unique_id);
  fs::create_directories(temp_root);
  return temp_root;
}

void cleanup_path(const fs::path& path) {
  std::error_code error;
  fs::remove_all(path, error);
}

void write_text_file(const fs::path& path, std::string_view content) {
  fs::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream << content;
}

[[nodiscard]] dasall::apps::cli::config::CliConfigWorkflowCoordinator make_coordinator(
    const fs::path& workspace) {
  using dasall::apps::cli::config::CliConfigWorkflowCoordinator;
  using dasall::apps::cli::config::CliConfigWorkflowDependencies;

  CliConfigWorkflowDependencies dependencies;
  dependencies.store_paths.defaults_file = workspace / "etc/default/dasall-daemon";
  dependencies.store_paths.daemon_config_file = workspace / "etc/dasall/daemon.json";
  dependencies.preflight_environment.daemon_binary_available = true;
  dependencies.preflight_environment.systemd_available = true;
  return CliConfigWorkflowCoordinator(std::move(dependencies));
}

void test_run_plan_projects_read_only_validate_action_for_current_state() {
  using dasall::apps::cli::CliCommand;
  using dasall::apps::cli::CliConfigCommandKind;
  using dasall::apps::cli::config::CliConfigWorkflowStatus;
  using dasall::tests::support::assert_true;

  const fs::path workspace = make_temp_directory("config-plan-workflow");
  write_text_file(workspace / "etc/default/dasall-daemon",
                  "DASALL_DAEMON_PROFILE_ID=desktop_full\n");
  write_text_file(workspace / "etc/dasall/daemon.json",
                  "{\n"
                  "  \"daemon\": {\n"
                  "    \"socket_path\": \"/run/dasall/daemon.sock\",\n"
                  "    \"log_format\": \"json\"\n"
                  "  }\n"
                  "}\n");

  auto coordinator = make_coordinator(workspace);
  CliCommand command;
  command.name = "config";
  command.config_command = CliConfigCommandKind::Plan;
  command.config_dry_run = true;

  const auto result = coordinator.run(command);
  cleanup_path(workspace);

  assert_true(result.handled && result.success,
              "config plan should render a read-only action plan for the current canonical state");
  assert_true(result.status == CliConfigWorkflowStatus::PlanRendered,
              "config plan should use the plan formatter path");
  assert_true(result.output.find("ConfiguredStopped") != std::string::npos &&
                  result.output.find("validate-only") != std::string::npos,
              "config plan should project state_before and the derived validate-only action");
}

void test_run_plan_from_file_projects_desired_state_diff() {
  using dasall::apps::cli::CliCommand;
  using dasall::apps::cli::CliConfigCommandKind;
  using dasall::tests::support::assert_true;

  const fs::path workspace = make_temp_directory("config-plan-from-file");
  write_text_file(workspace / "etc/default/dasall-daemon",
                  "DASALL_DAEMON_PROFILE_ID=factory_test\n");
  write_text_file(workspace / "etc/dasall/daemon.json",
                  "{\n"
                  "  \"daemon\": {\n"
                  "    \"socket_path\": \"/run/dasall/daemon.sock\",\n"
                  "    \"log_format\": \"json\",\n"
                  "    \"diag_enabled\": false,\n"
                  "    \"override_enabled\": false,\n"
                  "    \"watchdog_enabled\": false\n"
                  "  }\n"
                  "}\n");
  write_text_file(workspace / "desired.yaml",
                  "schema_version: dasall.config.apply.v1\n"
                  "profile_id: desktop_full\n"
                  "daemon:\n"
                  "  socket_path: /run/dasall/daemon.sock\n"
                  "  log_format: json\n"
                  "  diag_enabled: true\n"
                  "  override_enabled: false\n"
                  "  watchdog_enabled: false\n"
                  "service:\n"
                  "  start_now: false\n"
                  "  enable_on_boot: false\n"
                  "operator_access:\n"
                  "  add_users: []\n"
                  "secrets:\n"
                  "  refs: []\n");

  auto coordinator = make_coordinator(workspace);
  CliCommand command;
  command.name = "config";
  command.config_command = CliConfigCommandKind::Plan;
  command.config_from_file = (workspace / "desired.yaml").string();

  const auto result = coordinator.run(command);
  cleanup_path(workspace);

  assert_true(result.handled && result.success,
              "config plan --from-file should render a desired-state diff once ConfigDiffPlanner is available");
  assert_true(result.output.find("profile_id") != std::string::npos &&
                  result.output.find("daemon.diag_enabled") != std::string::npos,
              "config plan --from-file should project changed defaults and daemon keys into the action plan output");
}

}  // namespace

int main() {
  try {
    test_run_plan_projects_read_only_validate_action_for_current_state();
    test_run_plan_from_file_projects_desired_state_diff();
  } catch (const std::exception& ex) {
    std::cerr << "ConfigPlanWorkflowTest failed: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "ConfigPlanWorkflowTest passed\n";
  return 0;
}