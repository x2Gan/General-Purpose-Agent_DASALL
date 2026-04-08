#include <exception>
#include <filesystem>
#include <iostream>

#include "BuildProfileResolver.h"
#include "ProfileCatalog.h"
#include "ProfileError.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] std::filesystem::path repository_root() {
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

void test_build_profile_resolver_resolves_manifest_for_valid_profile() {
  using dasall::profiles::BuildProfileResolveRequest;
  using dasall::profiles::BuildProfileResolver;
  using dasall::profiles::ProfileCatalog;
  using dasall::tests::support::assert_true;

  const ProfileCatalog catalog(repository_root() / "profiles");
  const BuildProfileResolver resolver(catalog);

  const auto result = resolver.resolve_build_manifest(BuildProfileResolveRequest{
      .profile_id = "desktop_full",
      .expected_target_platform = std::string("linux-x86_64-workstation"),
  });

  assert_true(result.ok(), "resolver should produce build manifest for valid profile request");
  assert_true(result.has_consistent_values(),
              "resolved build manifest should satisfy consistency constraints");
  assert_true(result.manifest->enables_module("runtime"),
              "resolved build manifest should preserve runtime module enablement");
  assert_true(result.manifest->enables_adapter("llm_cloud_adapter"),
              "resolved build manifest should derive enabled adapters from module matrix");
}

void test_build_profile_resolver_rejects_unknown_profile() {
  using dasall::profiles::BuildProfileResolveRequest;
  using dasall::profiles::BuildProfileResolver;
  using dasall::profiles::ProfileCatalog;
  using dasall::profiles::ProfileErrorCode;
  using dasall::tests::support::assert_true;

  const ProfileCatalog catalog(repository_root() / "profiles");
  const BuildProfileResolver resolver(catalog);

  const auto result = resolver.resolve_build_manifest(BuildProfileResolveRequest{
      .profile_id = "unknown-profile",
      .expected_target_platform = std::nullopt,
  });

  assert_true(!result.ok(), "resolver should reject unknown profile requests");
  assert_true(result.error_code.has_value(), "unknown profile request should include error code");
  assert_true(*result.error_code == ProfileErrorCode::ProfileNotFound,
              "unknown profile request should map to profile-not-found error");
}

void test_build_profile_resolver_rejects_platform_mismatch_request() {
  using dasall::profiles::BuildProfileResolveRequest;
  using dasall::profiles::BuildProfileResolver;
  using dasall::profiles::ProfileCatalog;
  using dasall::profiles::ProfileErrorCode;
  using dasall::tests::support::assert_true;

  const ProfileCatalog catalog(repository_root() / "profiles");
  const BuildProfileResolver resolver(catalog);

  const auto result = resolver.resolve_build_manifest(BuildProfileResolveRequest{
      .profile_id = "desktop_full",
      .expected_target_platform = std::string("linux-arm64-embedded"),
  });

  assert_true(!result.ok(), "resolver should reject platform mismatched requests");
  assert_true(result.error_code.has_value(), "platform mismatch should include error code");
  assert_true(*result.error_code == ProfileErrorCode::PlatformMismatch,
              "platform mismatch should map to platform-mismatch error");
}

}  // namespace

int main() {
  try {
    test_build_profile_resolver_resolves_manifest_for_valid_profile();
    test_build_profile_resolver_rejects_unknown_profile();
    test_build_profile_resolver_rejects_platform_mismatch_request();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
