#include <exception>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <string>
#include <vector>

#include <unistd.h>

#include "DaemonConfig.h"
#include "DaemonConfigValidator.h"
#include "support/TestAssertions.h"

namespace {

namespace fs = std::filesystem;

[[nodiscard]] fs::path make_temp_directory(std::string_view stem) {
  const auto unique_id = std::to_string(::getpid()) + "-" +
                         std::to_string(fs::file_time_type::clock::now().time_since_epoch().count());
  const fs::path temp_root = fs::temp_directory_path() /
                             (std::string(stem) + "-" + unique_id);
  fs::create_directories(temp_root);
  return temp_root;
}

void cleanup_path(const fs::path& path) {
  std::error_code error;
  fs::remove_all(path, error);
}

void test_validate_config_accepts_v1_defaults() {
  using dasall::apps::daemon::DaemonBootstrapConfig;
  using dasall::apps::daemon::DaemonConfigValidator;
  using dasall::tests::support::assert_true;

  const DaemonConfigValidator validator;
  const auto result = validator.validate_config(DaemonBootstrapConfig{});
  assert_true(result.ok(), "DaemonConfigValidator should accept v1 default bootstrap config");
}

void test_validate_config_rejects_empty_socket_path() {
  using dasall::apps::daemon::DaemonBootstrapConfig;
  using dasall::apps::daemon::DaemonConfigValidationError;
  using dasall::apps::daemon::DaemonConfigValidator;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  DaemonBootstrapConfig config;
  config.socket_path.clear();

  const DaemonConfigValidator validator;
  const auto result = validator.validate_config(config);
  assert_true(!result.ok(), "DaemonConfigValidator should reject empty socket_path");
  assert_equal(static_cast<int>(DaemonConfigValidationError::InvalidSocketPath),
               static_cast<int>(*result.error_code),
               "empty socket_path should map to InvalidSocketPath");
}

void test_validate_config_rejects_path_traversal_components() {
  using dasall::apps::daemon::DaemonBootstrapConfig;
  using dasall::apps::daemon::DaemonConfigValidationError;
  using dasall::apps::daemon::DaemonConfigValidator;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  DaemonBootstrapConfig config;
  config.socket_path = "/tmp/dasall/../control.sock";

  const DaemonConfigValidator validator;
  const auto result = validator.validate_config(config);
  assert_true(!result.ok(), "DaemonConfigValidator should reject socket paths with traversal components");
  assert_equal(static_cast<int>(DaemonConfigValidationError::InvalidSocketPath),
               static_cast<int>(*result.error_code),
               "path traversal should map to InvalidSocketPath");
}

void test_validate_config_rejects_world_writable_parent_directory() {
  using dasall::apps::daemon::DaemonBootstrapConfig;
  using dasall::apps::daemon::DaemonConfigValidationError;
  using dasall::apps::daemon::DaemonConfigValidator;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const fs::path temp_root = make_temp_directory("daemon-config-validator");
  const fs::path socket_parent = temp_root / "socket-root";
  fs::create_directories(socket_parent);
  fs::permissions(socket_parent,
                  static_cast<fs::perms>(0777),
                  fs::perm_options::replace);

  DaemonBootstrapConfig config;
  config.socket_path = (socket_parent / "control.sock").string();

  const DaemonConfigValidator validator;
  const auto result = validator.validate_config(config);
  cleanup_path(temp_root);

  assert_true(!result.ok(), "DaemonConfigValidator should reject world-writable socket parent directories");
  assert_equal(static_cast<int>(DaemonConfigValidationError::InvalidSocketPath),
               static_cast<int>(*result.error_code),
               "unsafe parent directory should map to InvalidSocketPath");
}

void test_validate_config_rejects_payload_limit_above_upper_bound() {
  using dasall::apps::daemon::DaemonBootstrapConfig;
  using dasall::apps::daemon::DaemonConfigValidationError;
  using dasall::apps::daemon::DaemonConfigValidator;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  DaemonBootstrapConfig config;
  config.max_payload_bytes = 4U * 1048576U + 1U;

  const DaemonConfigValidator validator;
  const auto result = validator.validate_config(config);
  assert_true(!result.ok(), "DaemonConfigValidator should reject oversized max_payload_bytes");
  assert_equal(static_cast<int>(DaemonConfigValidationError::InvalidPayloadLimit),
               static_cast<int>(*result.error_code),
               "oversized payload limit should map to InvalidPayloadLimit");
}

void test_validate_conflicts_rejects_flag_and_config_file_mismatch() {
  using dasall::apps::daemon::DaemonConfigConflict;
  using dasall::apps::daemon::DaemonConfigSource;
  using dasall::apps::daemon::DaemonConfigValidationError;
  using dasall::apps::daemon::DaemonConfigValidator;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const DaemonConfigValidator validator;
  const auto result = validator.validate_conflicts({DaemonConfigConflict{
      .key = "daemon.socket_path",
      .first_source = DaemonConfigSource::CommandLine,
      .second_source = DaemonConfigSource::ConfigFile,
      .first_value = "/tmp/dasall-a.sock",
      .second_value = "/tmp/dasall-b.sock",
  }});

  assert_true(!result.ok(), "DaemonConfigValidator should reject flags/config conflicts");
  assert_equal(static_cast<int>(DaemonConfigValidationError::Conflict),
               static_cast<int>(*result.error_code),
               "flags/config mismatch should map to Conflict");
  assert_equal(std::string("daemon.socket_path"), result.key_paths.front(),
               "conflict should surface the conflicting key");
}

void test_validate_reload_keys_rejects_restart_only_keys() {
  using dasall::apps::daemon::DaemonConfigValidationError;
  using dasall::apps::daemon::DaemonConfigValidator;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const DaemonConfigValidator validator;
  const auto result = validator.validate_reload_keys(
      {"daemon.socket_path", "daemon.log_format"});

  assert_true(!result.ok(), "DaemonConfigValidator should reject restart-only reload keys");
  assert_equal(static_cast<int>(DaemonConfigValidationError::ReloadForbidden),
               static_cast<int>(*result.error_code),
               "restart-only keys should map to ReloadForbidden");
  assert_equal(std::string("daemon.socket_path"), result.key_paths.front(),
               "reload rejection should surface the first forbidden key");
}

void test_validate_only_returns_success_without_listener_side_effects() {
  using dasall::apps::daemon::DaemonBootstrapConfig;
  using dasall::apps::daemon::DaemonConfigValidator;
  using dasall::tests::support::assert_true;

  const DaemonConfigValidator validator;
  const auto result = validator.validate_only(DaemonBootstrapConfig{});
  assert_true(result.ok(), "validate_only should succeed for valid config without listener side effects");
}

}  // namespace

int main() {
  try {
    test_validate_config_accepts_v1_defaults();
    test_validate_config_rejects_empty_socket_path();
    test_validate_config_rejects_path_traversal_components();
    test_validate_config_rejects_world_writable_parent_directory();
    test_validate_config_rejects_payload_limit_above_upper_bound();
    test_validate_conflicts_rejects_flag_and_config_file_mismatch();
    test_validate_reload_keys_rejects_restart_only_keys();
    test_validate_only_returns_success_without_listener_side_effects();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonConfigValidatorTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}