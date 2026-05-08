#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "config/DaemonConfigFileStore.h"
#include "support/TestAssertions.h"

namespace {

namespace fs = std::filesystem;

[[nodiscard]] fs::path test_workspace_root() {
  return fs::temp_directory_path() / "dasall-daemon-config-file-store-test";
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

[[nodiscard]] dasall::apps::cli::config::DesiredConfigSnapshot make_desired(
    std::string profile_id = "desktop_full") {
  using dasall::apps::cli::config::DesiredConfigSnapshot;

  DesiredConfigSnapshot desired;
  desired.profile_id = std::move(profile_id);
  return desired;
}

void test_write_desired_creates_canonical_files_and_loads_snapshot() {
  using dasall::apps::cli::config::DaemonConfigFileStore;
  using dasall::apps::cli::config::DaemonConfigFileStorePaths;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const fs::path workspace = test_workspace_root() / "success";
  fs::remove_all(workspace);

  DaemonConfigFileStore store(DaemonConfigFileStorePaths{
      .defaults_file = workspace / "etc/default/dasall-daemon",
      .daemon_config_file = workspace / "etc/dasall/daemon.json",
  });

  const auto result = store.write_desired(make_desired());
  assert_true(result.success,
              "DaemonConfigFileStore should write both canonical files for a well-formed desired snapshot");
  assert_true(fs::exists(store.paths().defaults_file),
              "DaemonConfigFileStore should create /etc/default equivalent when applying desired config");
  assert_true(fs::exists(store.paths().daemon_config_file),
              "DaemonConfigFileStore should create daemon.json when applying desired config");

  const auto snapshot = store.load_current();
  assert_true(snapshot.has_value(),
              "DaemonConfigFileStore should reload the freshly written canonical files");
  assert_equal(std::string("desktop_full"), *snapshot->profile_id,
               "DaemonConfigFileStore should recover the frozen profile selection key from defaults file");
  assert_true(snapshot->daemon_config_json.find("\"socket_path\": \"/run/dasall/daemon.sock\"") !=
                  std::string::npos,
              "DaemonConfigFileStore should write daemon.json with the shared canonical socket path");
}

void test_write_desired_rolls_back_when_second_file_write_fails() {
  using dasall::apps::cli::config::DaemonConfigFileStore;
  using dasall::apps::cli::config::DaemonConfigFileStorePaths;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const fs::path workspace = test_workspace_root() / "rollback";
  fs::remove_all(workspace);
  const fs::path defaults_path = workspace / "etc/default/dasall-daemon";
    const fs::path blocked_parent = workspace / "etc/dasall";
    const fs::path daemon_path = blocked_parent / "daemon.json";
  write_text_file(defaults_path,
                  "DASALL_DAEMON_PROFILE_ID=edge_balanced\n");
    write_text_file(blocked_parent, "not a directory\n");

  DaemonConfigFileStore store(DaemonConfigFileStorePaths{
      .defaults_file = defaults_path,
      .daemon_config_file = daemon_path,
  });

  const auto result = store.write_desired(make_desired("desktop_full"));
  assert_true(!result.success,
              "DaemonConfigFileStore should fail when one canonical target cannot be overwritten atomically");
  assert_true(result.rolled_back,
              "DaemonConfigFileStore should rollback already written files when a later file write fails");
  assert_equal(std::string("DASALL_DAEMON_PROFILE_ID=edge_balanced\n"),
               read_text_file(defaults_path),
               "DaemonConfigFileStore should restore the original defaults file after rollback");
}

void test_write_desired_preserves_existing_permissions() {
  using dasall::apps::cli::config::DaemonConfigFileStore;
  using dasall::apps::cli::config::DaemonConfigFileStorePaths;
  using dasall::tests::support::assert_true;

  const fs::path workspace = test_workspace_root() / "permissions";
  fs::remove_all(workspace);
  const fs::path defaults_path = workspace / "etc/default/dasall-daemon";
  const fs::path daemon_path = workspace / "etc/dasall/daemon.json";
  write_text_file(defaults_path,
                  "DASALL_DAEMON_PROFILE_ID=edge_balanced\n");
  write_text_file(daemon_path,
                  "{\n  \"daemon\": {\n    \"socket_path\": \"/run/dasall/daemon.sock\"\n  }\n}\n");
  fs::permissions(defaults_path,
                  fs::perms::owner_read | fs::perms::owner_write,
                  fs::perm_options::replace);
  fs::permissions(daemon_path,
                  fs::perms::owner_read | fs::perms::group_read,
                  fs::perm_options::replace);

  const auto defaults_perms_before = fs::status(defaults_path).permissions();
  const auto daemon_perms_before = fs::status(daemon_path).permissions();

  DaemonConfigFileStore store(DaemonConfigFileStorePaths{
      .defaults_file = defaults_path,
      .daemon_config_file = daemon_path,
  });
  const auto result = store.write_desired(make_desired("factory_test"));
  assert_true(result.success,
              "DaemonConfigFileStore should update existing canonical files with preserved permissions");
  assert_true(fs::status(defaults_path).permissions() == defaults_perms_before,
              "DaemonConfigFileStore should preserve defaults file permissions across atomic rename");
  assert_true(fs::status(daemon_path).permissions() == daemon_perms_before,
              "DaemonConfigFileStore should preserve daemon.json permissions across atomic rename");
}

void test_write_desired_rejects_invalid_existing_daemon_json() {
  using dasall::apps::cli::config::DaemonConfigFileStore;
  using dasall::apps::cli::config::DaemonConfigFileStorePaths;
  using dasall::tests::support::assert_true;

  const fs::path workspace = test_workspace_root() / "invalid-json";
  fs::remove_all(workspace);
  const fs::path defaults_path = workspace / "etc/default/dasall-daemon";
  const fs::path daemon_path = workspace / "etc/dasall/daemon.json";
  write_text_file(defaults_path,
                  "DASALL_DAEMON_PROFILE_ID=edge_balanced\n");
  write_text_file(daemon_path, "{ invalid json\n");

  DaemonConfigFileStore store(DaemonConfigFileStorePaths{
      .defaults_file = defaults_path,
      .daemon_config_file = daemon_path,
  });
  const auto result = store.write_desired(make_desired("factory_test"));
  assert_true(!result.success,
              "DaemonConfigFileStore should reject writes when existing daemon.json is not valid JSON");
  assert_true(result.error_message.find("not valid JSON") != std::string::npos,
              "DaemonConfigFileStore should surface invalid JSON as a deterministic file store error");
  assert_true(read_text_file(daemon_path) == "{ invalid json\n",
              "DaemonConfigFileStore should leave invalid daemon.json untouched when rejecting the write");
}

}  // namespace

int main() {
  try {
    test_write_desired_creates_canonical_files_and_loads_snapshot();
    test_write_desired_rolls_back_when_second_file_write_fails();
    test_write_desired_preserves_existing_permissions();
    test_write_desired_rejects_invalid_existing_daemon_json();
  } catch (const std::exception& ex) {
    std::cerr << "DaemonConfigFileStoreTest failed: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "DaemonConfigFileStoreTest passed\n";
  return 0;
}