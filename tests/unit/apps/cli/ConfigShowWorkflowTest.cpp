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

void test_run_show_projects_summary_from_canonical_files() {
  using dasall::apps::cli::CliCommand;
  using dasall::apps::cli::CliConfigCommandKind;
  using dasall::apps::cli::config::CliConfigWorkflowStatus;
  using dasall::tests::support::assert_true;

  const fs::path workspace = make_temp_directory("config-show-workflow");
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
  command.config_command = CliConfigCommandKind::Show;

  const auto result = coordinator.run(command);
  cleanup_path(workspace);

  assert_true(result.handled && result.success,
              "config show should succeed when canonical files are readable");
  assert_true(result.status == CliConfigWorkflowStatus::SummaryRendered,
              "config show should render the summary formatter output");
  assert_true(result.output.find("profile: desktop_full") != std::string::npos &&
                  result.output.find("daemon.socket_path: /run/dasall/daemon.sock") !=
                      std::string::npos,
              "config show should project profile and socket path from canonical files");
}

void test_run_show_surfaces_invalid_daemon_json_as_incomplete_item() {
  using dasall::apps::cli::CliCommand;
  using dasall::apps::cli::CliConfigCommandKind;
  using dasall::tests::support::assert_true;

  const fs::path workspace = make_temp_directory("config-show-invalid-json");
  write_text_file(workspace / "etc/default/dasall-daemon",
                  "DASALL_DAEMON_PROFILE_ID=edge_balanced\n");
  write_text_file(workspace / "etc/dasall/daemon.json", "{ invalid json\n");

  auto coordinator = make_coordinator(workspace);
  CliCommand command;
  command.name = "config";
  command.config_command = CliConfigCommandKind::Show;

  const auto result = coordinator.run(command);
  cleanup_path(workspace);

  assert_true(result.handled && result.success,
              "config show should still render summary output when daemon.json is syntactically invalid");
  assert_true(result.output.find("daemon_config_invalid") != std::string::npos,
              "config show should surface invalid daemon.json as a stable incomplete item");
}

}  // namespace

int main() {
  try {
    test_run_show_projects_summary_from_canonical_files();
    test_run_show_surfaces_invalid_daemon_json_as_incomplete_item();
  } catch (const std::exception& ex) {
    std::cerr << "ConfigShowWorkflowTest failed: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "ConfigShowWorkflowTest passed\n";
  return 0;
}