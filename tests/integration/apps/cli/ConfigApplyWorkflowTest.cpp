#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <unistd.h>

#include "config/CliConfigWorkflowCoordinator.h"
#include "support/TestAssertions.h"

#ifndef DASALL_REPOSITORY_ROOT
#error "DASALL_REPOSITORY_ROOT must be defined"
#endif

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
    const fs::path& workspace) {
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
  dependencies.service_command_runner = [](const auto&) {
    return dasall::apps::cli::config::ServiceCommandResult{};
  };
  dependencies.secret_root_dir = workspace / "var/lib/dasall/secrets";
  return CliConfigWorkflowCoordinator(std::move(dependencies));
}

void test_apply_from_file_writes_canonical_files() {
  using dasall::apps::cli::CliCommand;
  using dasall::apps::cli::CliConfigCommandKind;
  using dasall::tests::support::assert_true;

  const fs::path workspace = make_temp_directory("config-apply-workflow");
  const fs::path fixture = fs::path(DASALL_REPOSITORY_ROOT) /
                           "tests/fixtures/apps/cli/config/desired_state_minimal.yaml";
  auto coordinator = make_coordinator(workspace);

  CliCommand command;
  command.name = "config";
  command.config_command = CliConfigCommandKind::Apply;
  command.config_from_file = fixture.string();
  command.no_input = true;

  const auto result = coordinator.run(command);

  assert_true(result.handled && result.success,
              "config apply --from-file should succeed for the minimal desired-state fixture");
  assert_true(fs::exists(workspace / "etc/default/dasall-daemon") &&
                  fs::exists(workspace / "etc/dasall/daemon.json"),
              "config apply --from-file should materialize both canonical files");
  assert_true(read_text_file(workspace / "etc/default/dasall-daemon")
                      .find("DASALL_DAEMON_PROFILE_ID=desktop_full") !=
                  std::string::npos,
              "config apply --from-file should write the requested profile_id into the defaults file");
  cleanup_path(workspace);
}

void test_apply_from_file_rolls_summary_to_failed_state_when_write_fails() {
  using dasall::apps::cli::CliCommand;
  using dasall::apps::cli::CliConfigCommandKind;
  using dasall::tests::support::assert_true;

  const fs::path workspace = make_temp_directory("config-apply-rollback");
  const fs::path fixture = fs::path(DASALL_REPOSITORY_ROOT) /
                           "tests/fixtures/apps/cli/config/desired_state_minimal.yaml";
  write_text_file(workspace / "etc/default/dasall-daemon",
                  "DASALL_DAEMON_PROFILE_ID=edge_balanced\n");
  write_text_file(workspace / "etc/dasall", "not a directory\n");
  auto coordinator = make_coordinator(workspace);

  CliCommand command;
  command.name = "config";
  command.config_command = CliConfigCommandKind::Apply;
  command.config_from_file = fixture.string();
  command.no_input = true;

  const auto result = coordinator.run(command);

  assert_true(result.handled && !result.success,
              "config apply --from-file should fail when canonical writes cannot complete");
  assert_true(result.output.find("apply_failed_rolled_back") != std::string::npos ||
                  result.output.find("blocked") != std::string::npos,
              "config apply --from-file should surface a failed apply outcome in its rendered summary");
  assert_true(read_text_file(workspace / "etc/default/dasall-daemon")
                      .find("edge_balanced") != std::string::npos,
              "config apply --from-file should preserve the previous defaults file when the daemon.json write fails");
  cleanup_path(workspace);
}

void test_apply_from_file_materializes_secret_from_owner_only_import_file() {
  using dasall::apps::cli::CliCommand;
  using dasall::apps::cli::CliConfigCommandKind;
  using dasall::tests::support::assert_true;

  const fs::path workspace = make_temp_directory("config-apply-secret-import");
  const fs::path import_file = workspace / "secrets/provider.key";
  const fs::path desired_file = workspace / "desired-secret.yaml";
  write_text_file(import_file, "deepseek-file-secret\n");
  std::error_code permissions_error;
  fs::permissions(import_file,
                  fs::perms::owner_read | fs::perms::owner_write,
                  fs::perm_options::replace,
                  permissions_error);
  write_text_file(desired_file,
            std::string("schema_version: dasall.config.apply.v1\n") +
              "profile_id: desktop_full\n"
              "daemon:\n"
              "  socket_path: /run/dasall/daemon.sock\n"
              "  log_format: json\n"
              "  diag_enabled: false\n"
              "  override_enabled: false\n"
              "  watchdog_enabled: false\n"
              "service:\n"
              "  start_now: false\n"
              "  enable_on_boot: false\n"
              "operator_access:\n"
              "  add_users: []\n"
              "secrets:\n"
              "  refs:\n"
              "    - ref: secret://llm/providers/deepseek-prod\n"
              "      source: file:" +
              import_file.string() +
              "\n"
              "      auth_profile_name: primary\n");

  auto coordinator = make_coordinator(workspace);

  CliCommand command;
  command.name = "config";
  command.config_command = CliConfigCommandKind::Apply;
  command.config_from_file = desired_file.string();
  command.no_input = true;

  const auto result = coordinator.run(command);
  const auto secret_text = read_text_file(
      workspace / "var/lib/dasall/secrets/llm/providers/deepseek-prod.secret");

  assert_true(result.handled && result.success,
              "config apply --from-file should materialize owner-only import-file secrets through the LLM secret page and bootstrap writer");
  assert_true(result.output.find("secret://llm/providers/deepseek-prod (configured)") != std::string::npos,
              "config apply summary should project only the redacted secret ref after file-backed onboarding succeeds");
  assert_true(secret_text.find("deepseek-file-secret") == std::string::npos,
              "config apply --from-file should not persist the plaintext secret into the bootstrap backend file");

  cleanup_path(workspace);
}

}  // namespace

int main() {
  try {
    test_apply_from_file_writes_canonical_files();
    test_apply_from_file_rolls_summary_to_failed_state_when_write_fails();
    test_apply_from_file_materializes_secret_from_owner_only_import_file();
  } catch (const std::exception& ex) {
    std::cerr << "ConfigApplyWorkflowTest failed: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "ConfigApplyWorkflowTest passed\n";
  return 0;
}