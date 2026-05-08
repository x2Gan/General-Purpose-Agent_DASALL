#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
  dependencies.prompt_input_handler = std::move(input_handler);
  dependencies.prompt_confirm_handler = std::move(confirm_handler);
  return CliConfigWorkflowCoordinator(std::move(dependencies));
}

void test_interactive_wizard_reuses_current_values_for_existing_config() {
  using dasall::apps::cli::CliCommand;
  using dasall::apps::cli::CliConfigCommandKind;
  using dasall::apps::cli::config::ConfirmRequest;
  using dasall::apps::cli::config::PromptRequest;
  using dasall::tests::support::assert_true;

  const fs::path workspace = make_temp_directory("config-existing-wizard");
  write_text_file(workspace / "etc/default/dasall-daemon",
                  "DASALL_DAEMON_PROFILE_ID=edge_balanced\n");
  write_text_file(workspace / "etc/dasall/daemon.json",
                  "{\n"
                  "  \"daemon\": {\n"
                  "    \"socket_path\": \"/run/dasall/daemon.sock\",\n"
                  "    \"log_format\": \"text\",\n"
                  "    \"diag_enabled\": false,\n"
                  "    \"override_enabled\": false,\n"
                  "    \"watchdog_enabled\": false\n"
                  "  }\n"
                  "}\n");

  std::vector<PromptRequest> seen_prompts;
  std::vector<std::string> seen_confirms;
  std::vector<std::optional<bool>> confirm_answers = {
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
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
              "existing-config wizard should succeed when the operator accepts current values and confirms apply");
  assert_true(seen_prompts.size() == 3 &&
                  seen_prompts[0].default_value == "edge_balanced" &&
                  seen_prompts[1].default_value == "/run/dasall/daemon.sock" &&
                  seen_prompts[2].default_value == "text",
              "interactive config wizard should reuse the current profile and daemon settings as prompt defaults");
  assert_true(seen_confirms.back().find("file_writes:\n- (none)") != std::string::npos,
              "interactive config wizard review should project an empty file-write diff when the operator keeps current values");
  assert_true(read_text_file(workspace / "etc/default/dasall-daemon")
                      .find("DASALL_DAEMON_PROFILE_ID=edge_balanced") !=
                  std::string::npos &&
                  read_text_file(workspace / "etc/dasall/daemon.json")
                      .find("\"log_format\": \"text\"") != std::string::npos,
              "interactive config wizard should preserve current canonical values when the operator accepts all defaults");
  assert_true(result.output.find("profile: edge_balanced") != std::string::npos &&
                  result.output.find("daemon.log_format: text") != std::string::npos,
              "interactive config wizard summary should reflect the current values that were reused during the edit flow");

  cleanup_path(workspace);
}

}  // namespace

int main() {
  try {
    test_interactive_wizard_reuses_current_values_for_existing_config();
  } catch (const std::exception& ex) {
    std::cerr << "ConfigInteractiveWizardTest failed: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "ConfigInteractiveWizardTest passed\n";
  return 0;
}