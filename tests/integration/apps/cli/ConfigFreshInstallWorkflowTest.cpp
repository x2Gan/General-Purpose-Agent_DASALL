#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

#include <unistd.h>

#include "CliBinaryTestSupport.h"
#include "config/CliConfigWorkflowCoordinator.h"
#include "support/TestAssertions.h"

#ifndef DASALL_CLI_BINARY_PATH
#error "DASALL_CLI_BINARY_PATH must be defined"
#endif

#ifndef DASALL_REPOSITORY_ROOT
#error "DASALL_REPOSITORY_ROOT must be defined"
#endif

namespace {

namespace fs = std::filesystem;

using dasall::tests::integration::access_support::run_process_capture_split;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

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

[[nodiscard]] std::string read_text_file(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

[[nodiscard]] dasall::apps::cli::config::CliConfigWorkflowCoordinator make_coordinator(
    const fs::path& workspace,
    dasall::apps::cli::config::InteractivePromptEngine::InputHandler input_handler,
    dasall::apps::cli::config::InteractivePromptEngine::ConfirmHandler confirm_handler) {
  using dasall::apps::cli::config::CliConfigWorkflowCoordinator;
  using dasall::apps::cli::config::CliConfigWorkflowDependencies;
  using dasall::apps::cli::config::PrivilegeContext;
  using dasall::apps::cli::config::ValidateOnlyResult;

  CliConfigWorkflowDependencies dependencies;
  dependencies.store_paths.defaults_file = workspace / "etc/default/dasall-daemon";
  dependencies.store_paths.daemon_config_file = workspace / "etc/dasall/daemon.json";
  dependencies.preflight_environment.daemon_binary_available = true;
  dependencies.preflight_environment.systemd_available = true;
  dependencies.privilege_context = PrivilegeContext{
      .running_as_root = true,
      .stdin_is_tty = true,
  };
  dependencies.validate_only_runner = [](const auto&) {
    return ValidateOnlyResult{
        .exit_code = 0,
        .stdout_text = "config validation passed",
        .stderr_text = {},
    };
  };
  dependencies.service_command_runner = [](const auto&) {
    return dasall::apps::cli::config::ServiceCommandResult{};
  };
  dependencies.prompt_input_handler = std::move(input_handler);
  dependencies.prompt_confirm_handler = std::move(confirm_handler);
  return CliConfigWorkflowCoordinator(std::move(dependencies));
}

void test_fresh_install_entrypoint_is_discoverable_via_help() {
  const fs::path cli_binary = DASALL_CLI_BINARY_PATH;
  const fs::path repository_root = DASALL_REPOSITORY_ROOT;
  assert_true(fs::exists(cli_binary),
              "config integration topology should build the dasall CLI binary before help smoke runs");

  const auto result = run_process_capture_split(
      {cli_binary.string(), "config", "--help"}, repository_root);

  assert_equal(result.exit_code, 0,
               "config integration topology smoke should expose config help without daemon or install-state prerequisites");
  assert_true(result.stdout_text.find("dasall-cli config") != std::string::npos,
              "config integration topology smoke should print the config command family help text");
  assert_true(result.stdout_text.find("config show") != std::string::npos &&
                  result.stdout_text.find("config plan") != std::string::npos &&
                  result.stdout_text.find("config validate") != std::string::npos,
              "config integration topology smoke should list the non-interactive config entrypoints");
}

void test_fresh_install_wizard_reuses_defaults_and_applies_plan() {
  using dasall::apps::cli::CliCommand;
  using dasall::apps::cli::CliConfigCommandKind;
  using dasall::apps::cli::config::ConfirmRequest;
  using dasall::apps::cli::config::PromptRequest;

  const fs::path workspace = make_temp_directory("config-fresh-install-wizard");
  std::vector<PromptRequest> seen_prompts;
  std::vector<std::string> seen_confirms;
  std::vector<std::optional<bool>> confirm_answers = {
      std::nullopt,
      std::nullopt,
      std::nullopt,
      true,
      false,
      true,
  };
  std::size_t confirm_index = 0;

  auto coordinator = make_coordinator(
      workspace,
      [&seen_prompts](const PromptRequest& request) -> std::optional<std::string> {
        seen_prompts.push_back(request);
        return std::string();
      },
      [&seen_confirms, &confirm_answers, &confirm_index](
          const ConfirmRequest& request) -> std::optional<bool> {
        seen_confirms.push_back(request.message);
        return confirm_answers.at(confirm_index++);
      });

  CliCommand command;
  command.name = "config";
  command.config_command = CliConfigCommandKind::Wizard;

  const auto result = coordinator.run(command);

  assert_true(result.handled && result.success,
              "fresh-install config wizard should succeed when defaults are accepted and review is confirmed");
  assert_true(fs::exists(workspace / "etc/default/dasall-daemon") &&
                  fs::exists(workspace / "etc/dasall/daemon.json"),
              "fresh-install config wizard should materialize both canonical files");
  assert_true(seen_prompts.size() == 3 &&
                  seen_prompts[0].default_value == "desktop_full" &&
                  seen_prompts[1].default_value == "/run/dasall/daemon.sock" &&
                  seen_prompts[2].default_value == "json",
              "fresh-install config wizard should prefill profile and daemon prompts with P0 defaults");
  assert_true(read_text_file(workspace / "etc/default/dasall-daemon")
                      .find("DASALL_DAEMON_PROFILE_ID=desktop_full") !=
                  std::string::npos,
              "fresh-install config wizard should persist the default desktop_full profile when the operator accepts defaults");
  assert_true(seen_confirms.back().find("[ReviewAndApplyPage]") != std::string::npos &&
                  seen_confirms.back().find("[OperatorAccessPage]") != std::string::npos &&
                  seen_confirms.back().find("dasall group") == std::string::npos,
              "fresh-install config wizard review should surface the P0 operator access hint without reviving group-based guidance");
  assert_true(result.output.find("apply_outcome: applied") != std::string::npos,
              "fresh-install config wizard should end with a summary page showing an applied outcome");

  cleanup_path(workspace);
}

}  // namespace

int main() {
  try {
    test_fresh_install_entrypoint_is_discoverable_via_help();
    test_fresh_install_wizard_reuses_defaults_and_applies_plan();
  } catch (const std::exception& ex) {
    std::cerr << "ConfigFreshInstallWorkflowTest failed: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "ConfigFreshInstallWorkflowTest passed\n";
  return 0;
}