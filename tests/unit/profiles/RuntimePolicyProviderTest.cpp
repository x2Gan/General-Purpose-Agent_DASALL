#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "ProfileCatalog.h"
#include "ProfileError.h"
#include "RuntimePolicyProvider.h"
#include "dasall/tests/support/TestAssertions.h"

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
  assert_true(*load_result.error_code == ProfileErrorCode::ProfileNotFound,
              "unknown profile should map to profile-not-found error");
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
             "\tsupport_level: ga\n");

  const ProfileCatalog catalog(temp_root);
  const RuntimePolicyProvider provider(catalog);

  const auto load_result = provider.load_snapshot(RuntimePolicyLoadRequest{.profile_id = "invalid"});

  assert_true(!load_result.ok(), "runtime policy provider should reject schema-incomplete snapshots");
  assert_true(load_result.error_code.has_value(), "schema-incomplete snapshot should include error code");
  assert_true(*load_result.error_code == ProfileErrorCode::SchemaInvalid,
              "schema-incomplete snapshot should map to schema-invalid error");

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
    test_runtime_policy_provider_activates_loaded_snapshot();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
