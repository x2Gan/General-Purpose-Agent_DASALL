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

void test_run_validate_executes_validate_only_runner() {
  using dasall::apps::cli::CliCommand;
  using dasall::apps::cli::CliConfigCommandKind;
  using dasall::apps::cli::config::CliConfigWorkflowCoordinator;
  using dasall::apps::cli::config::CliConfigWorkflowDependencies;
  using dasall::apps::cli::config::CliConfigWorkflowStatus;
  using dasall::apps::cli::config::ValidateOnlyResult;
  using dasall::tests::support::assert_true;

  const fs::path workspace = make_temp_directory("config-validate-workflow");
  write_text_file(workspace / "etc/default/dasall-daemon",
                  "DASALL_DAEMON_PROFILE_ID=desktop_full\n");
  write_text_file(workspace / "etc/dasall/daemon.json",
                  "{\n"
                  "  \"daemon\": {\n"
                  "    \"socket_path\": \"/run/dasall/daemon.sock\",\n"
                  "    \"log_format\": \"json\"\n"
                  "  }\n"
                  "}\n");

  bool invoked = false;
  CliConfigWorkflowDependencies dependencies;
  dependencies.store_paths.defaults_file = workspace / "etc/default/dasall-daemon";
  dependencies.store_paths.daemon_config_file = workspace / "etc/dasall/daemon.json";
  dependencies.preflight_environment.daemon_binary_available = true;
  dependencies.preflight_environment.systemd_available = true;
  dependencies.validate_only_runner = [&invoked](const auto& command) {
    invoked = true;
    dasall::tests::support::assert_true(
      command.size() >= 5 && command[0] == "/usr/sbin/dasall-daemon" &&
        command[1] == "--validate-only" &&
            command[2] == "--config-file",
      "config validate should invoke validate-only with the packaged daemon binary and canonical config-file arguments");
    return ValidateOnlyResult{
        .exit_code = 0,
        .stdout_text = "config validation passed",
        .stderr_text = {},
    };
  };
  CliConfigWorkflowCoordinator coordinator(std::move(dependencies));

  CliCommand command;
  command.name = "config";
  command.config_command = CliConfigCommandKind::Validate;

  const auto result = coordinator.run(command);
  cleanup_path(workspace);

  assert_true(invoked,
              "config validate should delegate to the injected validate-only runner");
  assert_true(result.handled && result.success,
              "config validate should succeed when validate-only returns zero");
  assert_true(result.status == CliConfigWorkflowStatus::ValidationRendered &&
                  result.output.find("validation: passed") != std::string::npos,
              "config validate should render a dedicated validation summary on success");
}

void test_run_validate_surfaces_validate_only_failure() {
  using dasall::apps::cli::CliCommand;
  using dasall::apps::cli::CliConfigCommandKind;
  using dasall::apps::cli::config::CliConfigWorkflowCoordinator;
  using dasall::apps::cli::config::CliConfigWorkflowDependencies;
  using dasall::apps::cli::config::ValidateOnlyResult;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const fs::path workspace = make_temp_directory("config-validate-failure");
  write_text_file(workspace / "etc/default/dasall-daemon",
                  "DASALL_DAEMON_PROFILE_ID=desktop_full\n");
  write_text_file(workspace / "etc/dasall/daemon.json",
                  "{\n"
                  "  \"daemon\": {\n"
                  "    \"socket_path\": \"/run/dasall/daemon.sock\",\n"
                  "    \"log_format\": \"json\"\n"
                  "  }\n"
                  "}\n");

  CliConfigWorkflowDependencies dependencies;
  dependencies.store_paths.defaults_file = workspace / "etc/default/dasall-daemon";
  dependencies.store_paths.daemon_config_file = workspace / "etc/dasall/daemon.json";
  dependencies.preflight_environment.daemon_binary_available = true;
  dependencies.preflight_environment.systemd_available = true;
  dependencies.validate_only_runner = [](const auto&) {
    return ValidateOnlyResult{
        .exit_code = 1,
        .stdout_text = {},
        .stderr_text = "config validation failed",
    };
  };
  CliConfigWorkflowCoordinator coordinator(std::move(dependencies));

  CliCommand command;
  command.name = "config";
  command.config_command = CliConfigCommandKind::Validate;

  const auto result = coordinator.run(command);
  cleanup_path(workspace);

  assert_true(result.handled && !result.success,
              "config validate should fail when validate-only returns non-zero");
  assert_equal(5, result.exit_code,
               "config validate should map deterministic validate-only failures to the business-failure exit family");
  assert_true(result.output.find("daemon_validate_only_failed") != std::string::npos,
              "config validate should surface the stable validate-only failure reason in its rendered output");
}

}  // namespace

int main() {
  try {
    test_run_validate_executes_validate_only_runner();
    test_run_validate_surfaces_validate_only_failure();
  } catch (const std::exception& ex) {
    std::cerr << "ConfigValidateWorkflowTest failed: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "ConfigValidateWorkflowTest passed\n";
  return 0;
}