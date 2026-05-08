#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "DaemonProfileProjection.h"
#include "ProfileCatalog.h"
#include "ProfileError.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] std::filesystem::path repository_root() {
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

[[nodiscard]] std::filesystem::path make_temp_directory() {
  const auto unique_suffix = std::to_string(
      std::chrono::high_resolution_clock::now().time_since_epoch().count());
  const auto temp_dir = std::filesystem::temp_directory_path() /
                        "dasall-daemon-profile-projection-test" / unique_suffix;
  std::filesystem::create_directories(temp_dir);
  return temp_dir;
}

void write_file(const std::filesystem::path& file_path, const std::string& content) {
  std::ofstream stream(file_path);
  stream << content;
}

void test_projection_loads_all_baseline_profiles_with_explicit_daemon_keys() {
  using dasall::profiles::DaemonProfileProjection;
  using dasall::profiles::DaemonProfileProjectionRequest;
  using dasall::profiles::ProfileCatalog;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const ProfileCatalog catalog(repository_root() / "profiles");
  const auto listed = catalog.list_profiles();
  assert_true(listed.ok(), "precondition: baseline profile catalog should be readable");
  assert_equal(5, static_cast<int>(listed.profiles.size()),
               "baseline daemon profile projection should cover five profiles");

  const DaemonProfileProjection projection(catalog);
  for (const auto& descriptor : listed.profiles) {
    const auto result = projection.load(DaemonProfileProjectionRequest{
        .profile_id = descriptor.profile_id,
    });

    assert_true(result.ok(), "daemon profile projection should load every baseline profile");
    assert_true(result.has_consistent_values(),
                "daemon profile projection result should stay structurally valid");
    assert_true(result.settings->defaulted_keys.empty(),
                "baseline profiles should now provide explicit daemon keys instead of relying on defaults");
  }
}

void test_projection_maps_explicit_daemon_profile_values() {
  using dasall::profiles::DaemonProfileProjection;
  using dasall::profiles::DaemonProfileProjectionRequest;
  using dasall::profiles::ProfileCatalog;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const ProfileCatalog catalog(repository_root() / "profiles");
  const DaemonProfileProjection projection(catalog);
  const auto result = projection.load(DaemonProfileProjectionRequest{
      .profile_id = "desktop_full",
  });

  assert_true(result.ok(), "desktop_full daemon profile projection should load");
  assert_equal(std::string("desktop_full"), result.settings->effective_profile_id,
               "projection should preserve profile id");
  assert_equal(std::string("/run/dasall/daemon.sock"), result.settings->socket_path,
               "projection should load explicit daemon socket_path");
  assert_equal(32, static_cast<int>(result.settings->listen_backlog),
               "projection should load explicit daemon listen_backlog");
  assert_equal(5000, result.settings->dispatch_timeout_ms,
               "projection should load explicit daemon dispatch timeout");
  assert_true(!result.settings->diag_enabled,
              "projection should keep daemon diagnostics disabled by default");
  assert_true(!result.settings->watchdog_enabled,
              "projection should keep daemon watchdog disabled by default");
}

void test_projection_rejects_invalid_daemon_key_values() {
  using dasall::profiles::DaemonProfileProjection;
  using dasall::profiles::DaemonProfileProjectionRequest;
  using dasall::profiles::ProfileCatalog;
  using dasall::profiles::ProfileErrorCode;
  using dasall::tests::support::assert_true;

  const std::filesystem::path temp_root = make_temp_directory();
  const std::filesystem::path invalid_profile = temp_root / "invalid_daemon";
  std::filesystem::create_directories(invalid_profile);

  write_file(invalid_profile / "profile.cmake",
             "set(DASALL_PROFILE_NAME \"invalid_daemon\")\n");
  write_file(invalid_profile / "runtime_policy.yaml",
             "schema_version: 1\n"
             "profile_meta:\n"
             "\tprofile_id: invalid_daemon\n"
             "\ttarget_platform: linux-x86_64\n"
             "\tsupport_level: ga\n"
             "daemon:\n"
             "\tsocket_path: /run/dasall/daemon.sock\n"
             "\tlisten_backlog: 0\n"
             "\tdispatch_timeout_ms: 5000\n"
             "\tdiag:\n"
             "\t\tenabled: false\n"
             "\twatchdog:\n"
             "\t\tenabled: false\n");

  const ProfileCatalog catalog(temp_root);
  const DaemonProfileProjection projection(catalog);
  const auto result = projection.load(DaemonProfileProjectionRequest{
      .profile_id = "invalid_daemon",
  });

  assert_true(!result.ok(), "daemon profile projection should reject invalid daemon schema values");
  assert_true(result.error_code.has_value() &&
                  *result.error_code == ProfileErrorCode::SchemaInvalid,
              "invalid daemon projection should map to schema invalid");

  std::filesystem::remove_all(temp_root);
}

}  // namespace

int main() {
  try {
    test_projection_loads_all_baseline_profiles_with_explicit_daemon_keys();
    test_projection_maps_explicit_daemon_profile_values();
    test_projection_rejects_invalid_daemon_key_values();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}