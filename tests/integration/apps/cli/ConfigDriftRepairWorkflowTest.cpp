#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
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

[[nodiscard]] std::string read_text_file(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

void seed_drifted_config(const fs::path& workspace) {
  write_text_file(workspace / "etc/default/dasall-daemon",
                  "DASALL_DAEMON_PROFILE_ID=edge_balanced\n");
  write_text_file(workspace / "etc/dasall/daemon.json", "{ invalid json\n");
}

[[nodiscard]] fs::path write_drift_repair_desired_state(const fs::path& workspace,
                                                        const bool start_now,
                                                        const bool enable_on_boot) {
  const fs::path desired_path = workspace / "desired-repair.yaml";
  std::string desired_yaml =
      "schema_version: dasall.config.apply.v1\n"
      "profile_id: edge_balanced\n"
      "daemon:\n"
      "  socket_path: /run/dasall/daemon.sock\n"
      "  log_format: text\n"
      "  diag_enabled: false\n"
      "  override_enabled: false\n"
      "  watchdog_enabled: false\n"
      "service:\n";
  desired_yaml += "  start_now: ";
  desired_yaml += start_now ? "true\n" : "false\n";
  desired_yaml += "  enable_on_boot: ";
  desired_yaml += enable_on_boot ? "true\n" : "false\n";
  desired_yaml +=
      "operator_access:\n"
      "  add_users: []\n"
      "secrets:\n"
      "  refs: []\n";
  write_text_file(desired_path, desired_yaml);
  return desired_path;
}

[[nodiscard]] dasall::apps::cli::config::CliConfigWorkflowCoordinator make_coordinator(
    const fs::path& workspace,
    const bool systemd_available,
    dasall::apps::cli::config::ServiceCommandRunner service_command_runner = {}) {
  using dasall::apps::cli::config::CliConfigWorkflowCoordinator;
  using dasall::apps::cli::config::CliConfigWorkflowDependencies;
  using dasall::apps::cli::config::PrivilegeContext;
  using dasall::apps::cli::config::ServiceCommandResult;
  using dasall::apps::cli::config::ValidateOnlyResult;

  CliConfigWorkflowDependencies dependencies;
  dependencies.store_paths.defaults_file = workspace / "etc/default/dasall-daemon";
  dependencies.store_paths.daemon_config_file = workspace / "etc/dasall/daemon.json";
  dependencies.preflight_environment.daemon_binary_available = true;
  dependencies.preflight_environment.systemd_available = systemd_available;
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
  dependencies.service_command_runner = service_command_runner
                                            ? std::move(service_command_runner)
                                            : dasall::apps::cli::config::ServiceCommandRunner(
                                                  [](const auto&) {
                                                    return ServiceCommandResult{};
                                                  });
  dependencies.secret_root_dir = workspace / "var/lib/dasall/secrets";
  return CliConfigWorkflowCoordinator(std::move(dependencies));
}

void test_show_surfaces_drifted_guidance_for_invalid_daemon_json() {
  using dasall::apps::cli::CliCommand;
  using dasall::apps::cli::CliConfigCommandKind;
  using dasall::tests::support::assert_true;

  const fs::path workspace = make_temp_directory("config-drifted-show");
  seed_drifted_config(workspace);
  auto coordinator = make_coordinator(workspace, true);

  CliCommand command;
  command.name = "config";
  command.config_command = CliConfigCommandKind::Show;

  const auto result = coordinator.run(command);

  assert_true(result.handled && result.success,
              "config show should keep rendering summary output for drifted deployments");
  assert_true(result.output.find("daemon_config_invalid") != std::string::npos,
              "drifted config show should surface invalid daemon.json as a stable incomplete item");
  assert_true(result.output.find(
                  "run 'dasall-cli config validate' to inspect deterministic config failures") !=
                  std::string::npos,
              "drifted config show should guide the operator toward validate-based repair");

  cleanup_path(workspace);
}

void test_apply_repairs_drifted_config_when_systemd_is_available() {
  using dasall::apps::cli::CliCommand;
  using dasall::apps::cli::CliConfigCommandKind;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const fs::path workspace = make_temp_directory("config-drift-repair-apply");
  seed_drifted_config(workspace);
  const fs::path desired_path =
      write_drift_repair_desired_state(workspace, false, false);

  std::size_t service_runner_invocations = 0;
  auto coordinator = make_coordinator(
      workspace,
      true,
      [&service_runner_invocations](const auto&) {
        ++service_runner_invocations;
        return dasall::apps::cli::config::ServiceCommandResult{};
      });

  CliCommand command;
  command.name = "config";
  command.config_command = CliConfigCommandKind::Apply;
  command.config_from_file = desired_path.string();
  command.no_input = true;
  command.output_mode = dasall::apps::cli::CliOutputMode::Json;

  const auto result = coordinator.run(command);
  const auto daemon_config_text =
      read_text_file(workspace / "etc/dasall/daemon.json");

  assert_true(result.handled && result.success,
          "config apply should repair drifted canonical files when systemd-backed service actions can complete");
  assert_equal(0,
               result.exit_code,
           "drift repair apply should keep exit code 0 after canonical repair and service restart succeed");
    assert_equal(std::size_t{1},
               service_runner_invocations,
           "systemd-backed drift repair should execute the planned restart exactly once");
  assert_true(daemon_config_text.find("\"log_format\": \"text\"") !=
                      std::string::npos,
              "drift repair apply should replace invalid daemon.json with the desired canonical content");
    assert_true(result.output.find("\"outcome\":\"applied\"") !=
              std::string::npos &&
            result.output.find("\"state_before\":\"Drifted\"") !=
              std::string::npos &&
            result.output.find("\"state_after\":\"ConfiguredRunning\"") !=
              std::string::npos,
          "drift repair apply should project the repaired apply outcome and Drifted-to-ConfiguredRunning transition in JSON summary output");

    cleanup_path(workspace);
  }

  void test_apply_degrades_service_actions_without_systemd() {
    using dasall::apps::cli::CliCommand;
    using dasall::apps::cli::CliConfigCommandKind;
    using dasall::tests::support::assert_equal;
    using dasall::tests::support::assert_true;

    const fs::path workspace = make_temp_directory("config-no-systemd-apply");
    seed_drifted_config(workspace);
    const fs::path desired_path =
      write_drift_repair_desired_state(workspace, true, true);

    std::size_t service_runner_invocations = 0;
    auto coordinator = make_coordinator(
      workspace,
      false,
      [&service_runner_invocations](const auto&) {
      ++service_runner_invocations;
      return dasall::apps::cli::config::ServiceCommandResult{};
      });

    CliCommand command;
    command.name = "config";
    command.config_command = CliConfigCommandKind::Apply;
    command.config_from_file = desired_path.string();
    command.no_input = true;
    command.output_mode = dasall::apps::cli::CliOutputMode::Json;

    const auto result = coordinator.run(command);

    assert_true(result.handled && result.success,
          "config apply should keep succeeding when non-systemd environments degrade service actions into follow-up work");
    assert_equal(0,
           result.exit_code,
           "non-systemd degraded apply should keep exit code 0 after canonical repair succeeds");
    assert_equal(std::size_t{0},
           service_runner_invocations,
           "non-systemd degraded apply should not invoke the service command runner when no systemctl commands can be planned");
    assert_true(result.output.find("\"outcome\":\"applied\"") !=
              std::string::npos &&
            result.output.find("\"state_before\":\"Unsupported\"") !=
              std::string::npos &&
            result.output.find("\"state_after\":\"ConfiguredStopped\"") !=
              std::string::npos,
          "non-systemd degraded apply should project Unsupported-to-ConfiguredStopped in JSON summary output");
    assert_true(result.output.find("\"service_restart\"") != std::string::npos &&
            result.output.find("\"service_enable\"") != std::string::npos &&
            result.output.find("systemd unavailable; automatic service actions must be performed manually") !=
                      std::string::npos,
              "non-systemd drift repair should degrade service actions into blocked_actions plus manual followups");

  cleanup_path(workspace);
}

}  // namespace

int main() {
  try {
    test_show_surfaces_drifted_guidance_for_invalid_daemon_json();
    test_apply_repairs_drifted_config_when_systemd_is_available();
    test_apply_degrades_service_actions_without_systemd();
  } catch (const std::exception& ex) {
    std::cerr << "ConfigDriftRepairWorkflowTest failed: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "ConfigDriftRepairWorkflowTest passed\n";
  return 0;
}