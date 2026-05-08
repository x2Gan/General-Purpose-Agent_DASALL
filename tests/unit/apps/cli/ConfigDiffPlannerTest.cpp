#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

#include <unistd.h>

#include "config/ConfigDiffPlanner.h"
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

void test_load_desired_from_file_and_build_plan_projects_canonical_changes() {
  using dasall::apps::cli::config::ConfigDiffPlanner;
  using dasall::apps::cli::config::DaemonConfigFileStorePaths;
  using dasall::apps::cli::config::DesiredConfigSnapshot;
  using dasall::apps::cli::config::InstallState;
  using dasall::tests::support::assert_true;

  const fs::path workspace = make_temp_directory("config-diff-planner");
  const fs::path desired_file = workspace / "desired.yaml";
  write_text_file(desired_file,
                  "schema_version: dasall.config.apply.v1\n"
                  "profile_id: desktop_full\n"
                  "daemon:\n"
                  "  socket_path: /run/dasall/daemon.sock\n"
                  "  log_format: json\n"
                  "  diag_enabled: true\n"
                  "  override_enabled: false\n"
                  "  watchdog_enabled: false\n"
                  "service:\n"
                  "  start_now: true\n"
                  "  enable_on_boot: false\n"
                  "operator_access:\n"
                  "  add_users: []\n"
                  "secrets:\n"
                  "  refs: []\n");

  DesiredConfigSnapshot current;
  current.profile_id = "factory_test";
  current.daemon.diag_enabled = false;

  ConfigDiffPlanner planner;
  std::string error_message;
  const auto desired = planner.load_desired_from_file(desired_file, &error_message);
  const auto plan = planner.build_plan(
      current,
      *desired,
      InstallState::BootstrapPending,
      DaemonConfigFileStorePaths{
          .defaults_file = workspace / "etc/default/dasall-daemon",
          .daemon_config_file = workspace / "etc/dasall/daemon.json",
      });
  cleanup_path(workspace);

  assert_true(desired.has_value() && error_message.empty(),
              "ConfigDiffPlanner should parse the minimal desired-state schema from file");
  assert_true(plan.file_writes.size() == 2 && plan.service_validate_requested &&
                  plan.service_start_requested && plan.service_restart_required,
              "ConfigDiffPlanner should plan canonical defaults+daemon writes and the derived validate/start actions");
}

void test_load_desired_from_file_rejects_unsupported_schema() {
  using dasall::apps::cli::config::ConfigDiffPlanner;
  using dasall::tests::support::assert_true;

  const fs::path workspace = make_temp_directory("config-diff-invalid-schema");
  const fs::path desired_file = workspace / "desired.yaml";
  write_text_file(desired_file,
                  "schema_version: dasall.config.apply.v0\n"
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
                  "  refs: []\n");

  ConfigDiffPlanner planner;
  std::string error_message;
  const auto desired = planner.load_desired_from_file(desired_file, &error_message);
  cleanup_path(workspace);

  assert_true(!desired.has_value() &&
                  error_message.find("unsupported desired-state schema_version") !=
                      std::string::npos,
              "ConfigDiffPlanner should fail closed when the desired-state schema_version is not the frozen v1 value");
}

}  // namespace

int main() {
  try {
    test_load_desired_from_file_and_build_plan_projects_canonical_changes();
    test_load_desired_from_file_rejects_unsupported_schema();
  } catch (const std::exception& ex) {
    std::cerr << "ConfigDiffPlannerTest failed: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "ConfigDiffPlannerTest passed\n";
  return 0;
}