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

void test_show_and_validate_share_the_same_observed_state_pipeline() {
  using dasall::apps::cli::CliCommand;
  using dasall::apps::cli::CliConfigCommandKind;
  using dasall::apps::cli::CliOutputMode;
  using dasall::apps::cli::config::CliConfigWorkflowCoordinator;
  using dasall::apps::cli::config::CliConfigWorkflowDependencies;
  using dasall::apps::cli::config::ValidateOnlyResult;
  using dasall::tests::support::assert_true;

  const fs::path workspace = make_temp_directory("config-show-validate-integration");
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
        .exit_code = 0,
        .stdout_text = "config validation passed",
        .stderr_text = {},
    };
  };
  CliConfigWorkflowCoordinator coordinator(std::move(dependencies));

  CliCommand show_command;
  show_command.name = "config";
  show_command.config_command = CliConfigCommandKind::Show;
  show_command.output_mode = CliOutputMode::Json;

  CliCommand validate_command;
  validate_command.name = "config";
  validate_command.config_command = CliConfigCommandKind::Validate;

  const auto show_result = coordinator.run(show_command);
  const auto validate_result = coordinator.run(validate_command);
  cleanup_path(workspace);

  assert_true(show_result.success &&
                  show_result.output.find("\"profile_id\":\"desktop_full\"") !=
                      std::string::npos,
              "config integration should project JSON summary output from the shared observed-state loader");
  assert_true(validate_result.success &&
                  validate_result.output.find("validation: passed") !=
                      std::string::npos,
              "config integration should reuse the same canonical snapshot when driving validate-only preflight");
}

}  // namespace

int main() {
  try {
    test_show_and_validate_share_the_same_observed_state_pipeline();
  } catch (const std::exception& ex) {
    std::cerr << "ConfigShowValidateIntegrationTest failed: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "ConfigShowValidateIntegrationTest passed\n";
  return 0;
}