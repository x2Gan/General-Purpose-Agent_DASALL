#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "DaemonEntryConfigLoader.h"
#include "support/TestAssertions.h"

namespace {

namespace fs = std::filesystem;

[[nodiscard]] fs::path repository_root() {
  return fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path().parent_path();
}

[[nodiscard]] fs::path make_temp_directory() {
  const auto unique_suffix = std::to_string(
      std::chrono::high_resolution_clock::now().time_since_epoch().count());
  const auto temp_dir = fs::temp_directory_path() /
                        "dasall-daemon-entry-config-loader-test" /
                        unique_suffix;
  fs::create_directories(temp_dir);
  return temp_dir;
}

void write_file(const fs::path& file_path, const std::string& content) {
  std::ofstream stream(file_path);
  stream << content;
}

void test_loader_defaults_to_desktop_profile_projection() {
  using dasall::apps::daemon::DaemonEntryConfigLoader;
  using dasall::apps::daemon::DaemonEntryConfigLoadRequest;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const DaemonEntryConfigLoader loader;
  const auto result = loader.load(DaemonEntryConfigLoadRequest{
      .profiles_root = repository_root() / "profiles",
      .requested_profile_id = dasall::apps::daemon::kDefaultDaemonEntryProfileId,
      .deployment_config_path = std::nullopt,
      .socket_path_override = std::nullopt,
  });

  assert_true(result.ok() && result.entry_config.has_value(),
              "daemon entry loader should project the default desktop profile");
  assert_equal(std::string("desktop_full"),
               result.entry_config->requested_profile_id,
               "daemon entry loader should default requested profile to desktop_full");
  assert_equal(std::string("desktop_full"),
               result.entry_config->effective_profile_id,
               "daemon entry loader should preserve the effective profile id from runtime snapshot");
  assert_equal(std::string("/tmp/dasall/control.sock"),
               result.entry_config->bootstrap_config.socket_path,
               "daemon entry loader should keep baseline daemon socket_path from the desktop profile");
  assert_true(result.entry_config->config_revision.has_value() &&
                  result.entry_config->config_revision->find("generation=1") != std::string::npos,
              "daemon entry loader should stamp runtime snapshot generation into config revision");
}

void test_loader_applies_yaml_deployment_overrides() {
  using dasall::apps::daemon::DaemonEntryConfigLoader;
  using dasall::apps::daemon::DaemonEntryConfigLoadRequest;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto temp_root = make_temp_directory();
  const auto config_path = temp_root / "daemon-entry.yaml";
  write_file(config_path,
             "daemon:\n"
             "  socket_path: /tmp/dasall/entry-loader.sock\n"
             "  receipt_ttl_sec: 120\n"
             "  override_enabled: true\n"
             "  log_format: text\n");

  const DaemonEntryConfigLoader loader;
  const auto result = loader.load(DaemonEntryConfigLoadRequest{
      .profiles_root = repository_root() / "profiles",
      .requested_profile_id = "edge_balanced",
      .deployment_config_path = config_path,
      .socket_path_override = std::nullopt,
  });

  fs::remove_all(temp_root);

  assert_true(result.ok() && result.entry_config.has_value(),
              "daemon entry loader should accept deployment yaml overrides");
  assert_equal(std::string("edge_balanced"),
               result.entry_config->effective_profile_id,
               "daemon entry loader should preserve the requested effective profile");
  assert_equal(std::string("/tmp/dasall/entry-loader.sock"),
               result.entry_config->bootstrap_config.socket_path,
               "daemon entry loader should overlay deployment socket_path");
  assert_equal(120,
               result.entry_config->bootstrap_config.receipt_ttl_sec,
               "daemon entry loader should overlay deployment receipt ttl");
  assert_true(result.entry_config->bootstrap_config.override_enabled,
              "daemon entry loader should overlay deployment override enable");
  assert_equal(std::string("text"),
               result.entry_config->bootstrap_config.log_format,
               "daemon entry loader should overlay deployment log_format");
}

void test_loader_captures_socket_path_conflicts_from_json_deployment_snapshot() {
  using dasall::apps::daemon::DaemonConfigSource;
  using dasall::apps::daemon::DaemonEntryConfigLoader;
  using dasall::apps::daemon::DaemonEntryConfigLoadRequest;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto temp_root = make_temp_directory();
  const auto config_path = temp_root / "daemon-entry.json";
  write_file(config_path,
             "{\n"
             "  \"daemon\": {\n"
             "    \"socket_path\": \"/tmp/dasall/from-config.sock\",\n"
             "    \"dispatch_workers\": 8,\n"
             "    \"diag_enabled\": true\n"
             "  }\n"
             "}\n");

  const DaemonEntryConfigLoader loader;
  const auto result = loader.load(DaemonEntryConfigLoadRequest{
      .profiles_root = repository_root() / "profiles",
      .deployment_config_path = config_path,
      .socket_path_override = std::string("/tmp/dasall/from-cli.sock"),
  });

  fs::remove_all(temp_root);

  assert_true(result.ok() && result.entry_config.has_value(),
              "daemon entry loader should still return a candidate config when a flags/config conflict exists");
  assert_equal(static_cast<std::size_t>(1),
               result.entry_config->conflicts.size(),
               "daemon entry loader should record one socket_path conflict");
  assert_equal(std::string("daemon.socket_path"),
               result.entry_config->conflicts.front().key,
               "daemon entry loader should surface the conflicting socket_path key");
  assert_equal(static_cast<int>(DaemonConfigSource::CommandLine),
               static_cast<int>(result.entry_config->conflicts.front().first_source),
               "daemon entry loader should classify the first conflict source as command line");
  assert_equal(static_cast<int>(DaemonConfigSource::ConfigFile),
               static_cast<int>(result.entry_config->conflicts.front().second_source),
               "daemon entry loader should classify the second conflict source as config file");
  assert_equal(std::string("/tmp/dasall/from-cli.sock"),
               result.entry_config->bootstrap_config.socket_path,
               "daemon entry loader should preserve the explicit socket_path override as the candidate value");
  assert_true(result.entry_config->bootstrap_config.diag_enabled,
              "daemon entry loader should parse boolean deployment overrides from json");
}

}  // namespace

int main() {
  try {
    test_loader_defaults_to_desktop_profile_projection();
    test_loader_applies_yaml_deployment_overrides();
    test_loader_captures_socket_path_conflicts_from_json_deployment_snapshot();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonEntryConfigProjectionTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}