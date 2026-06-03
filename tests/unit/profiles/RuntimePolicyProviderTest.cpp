#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

#include "LastKnownGoodStore.h"
#include "ProfileCatalog.h"
#include "ProfileError.h"
#include "RuntimePolicyProvider.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] std::filesystem::path repository_root() {
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

[[nodiscard]] std::filesystem::path make_temp_directory() {
  const auto unique_suffix = std::to_string(
      std::chrono::high_resolution_clock::now().time_since_epoch().count());
  const auto temp_dir = std::filesystem::temp_directory_path() /
                        "dasall-runtime-policy-provider-test" / unique_suffix;
  std::filesystem::create_directories(temp_dir);
  return temp_dir;
}

void write_file(const std::filesystem::path& file_path, const std::string& content) {
  std::ofstream stream(file_path);
  stream << content;
}

void test_runtime_policy_provider_loads_snapshot_for_valid_profile() {
  using dasall::profiles::ProfileCatalog;
  using dasall::profiles::RuntimePolicyLoadRequest;
  using dasall::profiles::RuntimePolicyProvider;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const ProfileCatalog catalog(repository_root() / "profiles");
  const RuntimePolicyProvider provider(catalog);

  const auto load_result = provider.load_snapshot(RuntimePolicyLoadRequest{.profile_id = "desktop_full"});

  assert_true(load_result.ok(), "runtime policy provider should load valid baseline profile snapshot");
  assert_true(load_result.has_consistent_values(),
              "loaded runtime policy snapshot should preserve frozen policy constraints");
  assert_equal(std::string("desktop_full"), load_result.snapshot->effective_profile_id(),
               "loaded snapshot should preserve profile id");
  assert_equal(std::string("prom_text"),
               load_result.snapshot->ops_policy().optional_backends.metrics_exporter_type,
               "loaded snapshot should preserve the metrics exporter selected by the runtime policy asset");
  assert_equal(std::string("builtin:metrics-prom_text"),
               load_result.snapshot->ops_policy().optional_backends.metrics_exporter_package_asset,
               "loaded snapshot should preserve the metrics exporter package asset binding");
  assert_equal(std::string("file"),
               load_result.snapshot->ops_policy().optional_backends.trace_exporter_type,
               "loaded snapshot should preserve the trace exporter selected by the runtime policy asset");
  assert_equal(std::string("file"),
               load_result.snapshot->ops_policy().optional_backends.secret_backend_type,
               "loaded snapshot should preserve the secret backend selected by the runtime policy asset");
  assert_true(load_result.snapshot->memory_maintenance_policy().enabled,
              "loaded snapshot should preserve memory maintenance enablement");
  assert_equal(60000,
               static_cast<int>(load_result.snapshot->memory_maintenance_policy().interval_ms),
               "loaded snapshot should preserve memory maintenance cadence interval");
  assert_equal(std::string("passive_each_tick"),
               load_result.snapshot->memory_maintenance_policy().checkpoint_strategy,
               "loaded snapshot should preserve memory maintenance checkpoint strategy");
}

void test_runtime_policy_provider_rejects_unknown_profile_requests() {
  using dasall::profiles::ProfileCatalog;
  using dasall::profiles::ProfileErrorCode;
  using dasall::profiles::RuntimePolicyLoadRequest;
  using dasall::profiles::RuntimePolicyProvider;
  using dasall::tests::support::assert_true;

  const ProfileCatalog catalog(repository_root() / "profiles");
  const RuntimePolicyProvider provider(catalog);

  const auto load_result =
      provider.load_snapshot(RuntimePolicyLoadRequest{.profile_id = "not-exist-profile"});

  assert_true(!load_result.ok(), "runtime policy provider should reject unknown profile ids");
  assert_true(load_result.error_code.has_value(), "unknown profile should include error code");
  assert_true(*load_result.error_code == ProfileErrorCode::LastKnownGoodUnavailable,
              "unknown profile should map to last-known-good-unavailable when no fallback exists");
}

void test_runtime_policy_provider_rejects_invalid_schema_content() {
  using dasall::profiles::ProfileCatalog;
  using dasall::profiles::ProfileErrorCode;
  using dasall::profiles::RuntimePolicyLoadRequest;
  using dasall::profiles::RuntimePolicyProvider;
  using dasall::tests::support::assert_true;

  const std::filesystem::path temp_root = make_temp_directory();
  const std::filesystem::path invalid_profile = temp_root / "invalid";
  std::filesystem::create_directories(invalid_profile);

  write_file(invalid_profile / "profile.cmake", "set(DASALL_PROFILE_NAME \"invalid\")\n");
  write_file(invalid_profile / "runtime_policy.yaml",
             "schema_version: 1\n"
             "profile_meta:\n"
             "\tprofile_id: invalid\n"
             "\ttarget_platform: linux-x86_64\n"
             "\tsupport_level: ga\n"
             "memory:\n"
             "\tmaintenance:\n"
             "\t\tenabled: true\n"
             "\t\tinterval_ms: 60000\n"
             "\t\tjitter_ms: 5000\n"
             "\t\tretention_ms: 300000\n"
             "\t\tcheckpoint_strategy: passive_each_tick\n");

  const ProfileCatalog catalog(temp_root);
  const RuntimePolicyProvider provider(catalog);

  const auto load_result = provider.load_snapshot(RuntimePolicyLoadRequest{.profile_id = "invalid"});

  assert_true(!load_result.ok(), "runtime policy provider should reject schema-incomplete snapshots");
  assert_true(load_result.error_code.has_value(), "schema-incomplete snapshot should include error code");
  assert_true(*load_result.error_code == ProfileErrorCode::LastKnownGoodUnavailable,
              "schema-incomplete snapshot should map to lkg-unavailable when fallback is absent");

  std::filesystem::remove_all(temp_root);
}

void test_runtime_policy_provider_uses_last_known_good_fallback_when_schema_invalid() {
  using dasall::profiles::LastKnownGoodStore;
  using dasall::profiles::ProfileCatalog;
  using dasall::profiles::RuntimePolicyProvider;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto lkg_store = std::make_shared<LastKnownGoodStore>();

  const ProfileCatalog baseline_catalog(repository_root() / "profiles");
  RuntimePolicyProvider baseline_provider(baseline_catalog, lkg_store);
  const auto baseline_loaded = baseline_provider.load_snapshot({.profile_id = "desktop_full"});
  assert_true(baseline_loaded.ok(), "precondition: baseline snapshot should load before lkg fallback");
  const auto activated =
      baseline_provider.activate_snapshot({.snapshot = baseline_loaded.snapshot});
  assert_true(activated.ok(), "precondition: baseline snapshot should be persisted into lkg store");

  const std::filesystem::path temp_root = make_temp_directory();
  const std::filesystem::path broken_profile = temp_root / "desktop_full";
  std::filesystem::create_directories(broken_profile);

  write_file(broken_profile / "profile.cmake", "set(DASALL_PROFILE_NAME \"desktop_full\")\n");
  write_file(broken_profile / "runtime_policy.yaml",
             "schema_version: 1\n"
             "profile_meta:\n"
             "\tprofile_id: desktop_full\n"
             "\ttarget_platform: linux-x86_64-workstation\n"
             "\tsupport_level: ga\n"
             "memory:\n"
             "\tmaintenance:\n"
             "\t\tenabled: true\n"
             "\t\tinterval_ms: 60000\n"
             "\t\tjitter_ms: 5000\n"
             "\t\tretention_ms: 300000\n"
             "\t\tcheckpoint_strategy: passive_each_tick\n");

  const ProfileCatalog broken_catalog(temp_root);
  const RuntimePolicyProvider fallback_provider(broken_catalog, lkg_store);
  const auto fallback_loaded = fallback_provider.load_snapshot({.profile_id = "desktop_full"});

  assert_true(fallback_loaded.ok(), "provider should fallback to last-known-good snapshot on schema failure");
  assert_equal(std::string("desktop_full"), fallback_loaded.snapshot->effective_profile_id(),
               "fallback snapshot should preserve requested profile id");

  std::filesystem::remove_all(temp_root);
}

void test_runtime_policy_provider_activates_loaded_snapshot() {
  using dasall::profiles::ProfileCatalog;
  using dasall::profiles::RuntimePolicyActivateRequest;
  using dasall::profiles::RuntimePolicyProvider;
  using dasall::tests::support::assert_true;

  const ProfileCatalog catalog(repository_root() / "profiles");
  RuntimePolicyProvider provider(catalog);

  const auto load_result = provider.load_snapshot({.profile_id = "desktop_full"});
  assert_true(load_result.ok(), "precondition: load should succeed before activation");

  const auto activate_result =
      provider.activate_snapshot(RuntimePolicyActivateRequest{.snapshot = load_result.snapshot});

  assert_true(activate_result.ok(), "activate_snapshot should accept consistent snapshot");
  assert_true(provider.active_snapshot() != nullptr,
              "active_snapshot should expose currently activated snapshot");
}

}  // namespace

int main() {
  try {
    test_runtime_policy_provider_loads_snapshot_for_valid_profile();
    test_runtime_policy_provider_rejects_unknown_profile_requests();
    test_runtime_policy_provider_rejects_invalid_schema_content();
    test_runtime_policy_provider_uses_last_known_good_fallback_when_schema_invalid();
    test_runtime_policy_provider_activates_loaded_snapshot();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
