#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
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

void seed_existing_config(const fs::path& workspace) {
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
}

[[nodiscard]] fs::path write_desired_state(const fs::path& workspace) {
  const fs::path desired_path = workspace / "desired.yaml";
  write_text_file(desired_path,
                  "schema_version: dasall.config.apply.v1\n"
                  "profile_id: edge_balanced\n"
                  "daemon:\n"
                  "  socket_path: /run/dasall/daemon.sock\n"
                  "  log_format: json\n"
                  "  diag_enabled: false\n"
                  "  override_enabled: false\n"
                  "  watchdog_enabled: false\n"
                  "service:\n"
                  "  start_now: true\n"
                  "  enable_on_boot: true\n"
                  "operator_access:\n"
                  "  add_users: []\n"
                  "secrets:\n"
                  "  refs: []\n");
  return desired_path;
}

[[nodiscard]] dasall::apps::cli::config::CliConfigWorkflowCoordinator make_coordinator(
    const fs::path& workspace,
    dasall::apps::cli::config::ServiceCommandRunner service_command_runner) {
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
      .stdin_is_tty = false,
  };
  dependencies.validate_only_runner = [](const auto&) {
    return ValidateOnlyResult{
        .exit_code = 0,
        .stdout_text = "config validation passed",
        .stderr_text = {},
    };
  };
  dependencies.service_command_runner = std::move(service_command_runner);
  return CliConfigWorkflowCoordinator(std::move(dependencies));
}

void test_modify_existing_apply_executes_restart_and_enable_actions() {
  using dasall::apps::cli::CliCommand;
  using dasall::apps::cli::CliConfigCommandKind;
  using dasall::apps::cli::config::ServiceCommandResult;
  using dasall::tests::support::assert_true;

  const fs::path workspace = make_temp_directory("config-modify-existing-success");
  seed_existing_config(workspace);
  const fs::path desired_path = write_desired_state(workspace);

  std::vector<std::string> seen_actions;
  auto coordinator = make_coordinator(
      workspace,
      [&seen_actions](const auto& command) {
        seen_actions.emplace_back(
            std::string(dasall::apps::cli::config::to_string(command.action)));
        return ServiceCommandResult{};
      });

  CliCommand command;
  command.name = "config";
  command.config_command = CliConfigCommandKind::Apply;
  command.config_from_file = desired_path.string();
  command.no_input = true;

  const auto result = coordinator.run(command);

  assert_true(result.handled && result.success,
              "config apply should succeed when service restart and enable actions both succeed");
  assert_true(seen_actions.size() == 2 && seen_actions[0] == "restart" &&
                  seen_actions[1] == "enable",
              "config apply should execute restart then enable for existing-config daemon changes");
  assert_true(result.output.find("completed_actions:\n- restart\n- enable") !=
                  std::string::npos,
              "config apply summary should expose completed restart and enable actions");
  assert_true(result.output.find("manual_followups:\n- (none)") !=
                  std::string::npos,
              "config apply summary should not retain manual service followups after successful service execution");
  assert_true(read_text_file(workspace / "etc/dasall/daemon.json")
                      .find("\"log_format\": \"json\"") != std::string::npos,
              "config apply should persist the updated daemon.json before running service actions");

  cleanup_path(workspace);
}

void test_modify_existing_apply_keeps_file_changes_when_enable_fails() {
  using dasall::apps::cli::CliCommand;
  using dasall::apps::cli::CliConfigCommandKind;
  using dasall::apps::cli::config::ServiceCommandResult;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const fs::path workspace = make_temp_directory("config-modify-existing-failure");
  seed_existing_config(workspace);
  const fs::path desired_path = write_desired_state(workspace);

  std::vector<std::string> seen_actions;
  auto coordinator = make_coordinator(
      workspace,
      [&seen_actions](const auto& command) {
        const std::string action =
            std::string(dasall::apps::cli::config::to_string(command.action));
        seen_actions.push_back(action);
        if (action == "enable") {
          return ServiceCommandResult{
              .exit_code = 5,
              .stdout_text = {},
              .stderr_text = "permission denied",
          };
        }
        return ServiceCommandResult{};
      });

  CliCommand command;
  command.name = "config";
  command.config_command = CliConfigCommandKind::Apply;
  command.config_from_file = desired_path.string();
  command.no_input = true;

  const auto result = coordinator.run(command);

  assert_true(result.handled && !result.success,
              "config apply should fail when a post-write service action returns non-zero");
  assert_equal(6, result.exit_code,
               "config apply should map service action failures to the retryable blocked exit family");
  assert_true(seen_actions.size() == 2 && seen_actions[0] == "restart" &&
                  seen_actions[1] == "enable",
              "config apply should surface the failing enable action after a completed restart");
  assert_true(result.output.find("service action enable failed: permission denied") !=
                  std::string::npos,
              "config apply should surface the failing service action and stderr in its summary output");
  assert_true(result.output.find("completed_actions:\n- restart") !=
                  std::string::npos,
              "config apply should retain the successfully completed restart action in the rendered summary");
  assert_true(result.output.find("systemctl status dasall-daemon.service") !=
                  std::string::npos,
              "config apply should offer immediate operator followups when a service action fails");
  assert_true(read_text_file(workspace / "etc/dasall/daemon.json")
                      .find("\"log_format\": \"json\"") != std::string::npos,
              "config apply should keep persisted file changes when service enable fails after validation");

  cleanup_path(workspace);
}

}  // namespace

int main() {
  try {
    test_modify_existing_apply_executes_restart_and_enable_actions();
    test_modify_existing_apply_keeps_file_changes_when_enable_fails();
  } catch (const std::exception& ex) {
    std::cerr << "ConfigModifyExistingWorkflowTest failed: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "ConfigModifyExistingWorkflowTest passed\n";
  return 0;
}