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

void test_run_plan_rejects_from_file_until_diff_planner_exists() {
  using dasall::apps::cli::CliCommand;
  using dasall::apps::cli::CliConfigCommandKind;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto coordinator = make_coordinator(make_temp_directory("config-plan-from-file"));
  CliCommand command;
  command.name = "config";
  command.config_command = CliConfigCommandKind::Plan;
  command.config_from_file = "/tmp/dasall-config-desired.yaml";

  const auto result = coordinator.run(command);
  cleanup_path(fs::path(*command.config_from_file).parent_path().parent_path());

  assert_true(result.handled && !result.success,
              "config plan should fail closed when desired-state diff planning is not implemented yet");
  assert_equal(2, result.exit_code,
               "config plan --from-file should map to the local argument error exit family before CLCFG-TODO-013 lands");
  assert_true(result.output.find("CLCFG-TODO-013") != std::string::npos,
              "config plan --from-file should point at the pending diff planner task rather than silently ignoring the file input");
}

}  // namespace

int main() {
  try {
    test_run_plan_projects_read_only_validate_action_for_current_state();
    test_run_plan_rejects_from_file_until_diff_planner_exists();
  } catch (const std::exception& ex) {
    std::cerr << "ConfigPlanWorkflowTest failed: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "ConfigPlanWorkflowTest passed\n";
  return 0;
}