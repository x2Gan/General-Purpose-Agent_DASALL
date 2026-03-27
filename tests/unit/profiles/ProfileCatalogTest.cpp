#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "ProfileCatalog.h"
#include "ProfileError.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

[[nodiscard]] std::filesystem::path repository_root() {
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

[[nodiscard]] std::filesystem::path make_temp_directory() {
  const auto unique_suffix = std::to_string(
      std::chrono::high_resolution_clock::now().time_since_epoch().count());
  const auto temp_dir =
      std::filesystem::temp_directory_path() / "dasall-profile-catalog-test" / unique_suffix;
  std::filesystem::create_directories(temp_dir);
  return temp_dir;
}

void write_file(const std::filesystem::path& file_path, const std::string& content) {
  std::ofstream stream(file_path);
  stream << content;
}

void test_profile_catalog_discovers_five_profiles_and_unique_ids() {
  using dasall::profiles::ProfileCatalog;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const ProfileCatalog catalog(repository_root() / "profiles");
  const auto listed = catalog.list_profiles();

  assert_true(listed.ok(), "profile catalog should discover profiles from repository assets");
  assert_true(listed.has_consistent_values(),
              "profile catalog list should preserve descriptor and uniqueness constraints");
  assert_equal(5, static_cast<int>(listed.profiles.size()),
               "profile catalog should discover five baseline profile assets");
}

void test_profile_catalog_lookup_supports_found_and_missing_paths() {
  using dasall::profiles::ProfileCatalog;
  using dasall::profiles::ProfileErrorCode;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const ProfileCatalog catalog(repository_root() / "profiles");

  const auto found = catalog.get_profile("desktop_full");
  assert_true(found.ok(), "profile lookup should return descriptor for known profile id");
  assert_equal(std::string("desktop_full"), found.profile->profile_id,
               "profile lookup should preserve the requested profile id");

  const auto missing = catalog.get_profile("non-existent-profile");
  assert_true(!missing.ok(), "profile lookup should reject missing profile ids");
  assert_true(missing.error_code.has_value(), "missing profile should return error code");
  assert_true(*missing.error_code == ProfileErrorCode::ProfileNotFound,
              "missing profile should map to profile-not-found error");
}

void test_profile_catalog_rejects_duplicate_profile_ids() {
  using dasall::profiles::ProfileCatalog;
  using dasall::profiles::ProfileErrorCode;
  using dasall::tests::support::assert_true;

  const std::filesystem::path temp_root = make_temp_directory();
  const std::filesystem::path first_profile = temp_root / "a";
  const std::filesystem::path second_profile = temp_root / "b";
  std::filesystem::create_directories(first_profile);
  std::filesystem::create_directories(second_profile);

  write_file(first_profile / "profile.cmake", "set(DASALL_PROFILE_NAME \"same_id\")\n");
  write_file(second_profile / "profile.cmake", "set(DASALL_PROFILE_NAME \"same_id\")\n");

  const std::string yaml =
      "schema_version: 1\n"
      "profile_meta:\n"
      "\tprofile_id: same_id\n"
      "\ttarget_platform: linux-x86_64\n"
      "\tsupport_level: ga\n";

  write_file(first_profile / "runtime_policy.yaml", yaml);
  write_file(second_profile / "runtime_policy.yaml", yaml);

  const ProfileCatalog catalog(temp_root);
  const auto listed = catalog.list_profiles();

  assert_true(!listed.ok(), "profile catalog should reject duplicate profile ids");
  assert_true(listed.error_code.has_value(), "duplicate profile ids should return error code");
  assert_true(*listed.error_code == ProfileErrorCode::CatalogUnavailable,
              "duplicate profile ids should map to catalog-unavailable error");

  std::filesystem::remove_all(temp_root);
}

}  // namespace

int main() {
  try {
    test_profile_catalog_discovers_five_profiles_and_unique_ids();
    test_profile_catalog_lookup_supports_found_and_missing_paths();
    test_profile_catalog_rejects_duplicate_profile_ids();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
