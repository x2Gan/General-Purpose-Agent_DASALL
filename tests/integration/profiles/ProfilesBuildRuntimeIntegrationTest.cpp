#include <exception>
#include <filesystem>
#include <iostream>

#include "BuildProfileResolver.h"
#include "ProfileCatalog.h"
#include "ProfileError.h"
#include "RuntimePolicyProvider.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] std::filesystem::path repository_root() {
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

void test_profiles_build_and_runtime_paths_resolve_same_profile() {
  using dasall::profiles::BuildProfileResolveRequest;
  using dasall::profiles::BuildProfileResolver;
  using dasall::profiles::ProfileCatalog;
  using dasall::profiles::RuntimePolicyLoadRequest;
  using dasall::profiles::RuntimePolicyProvider;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const ProfileCatalog catalog(repository_root() / "profiles");
  const BuildProfileResolver resolver(catalog);
  const RuntimePolicyProvider provider(catalog);

  const auto manifest_result = resolver.resolve_build_manifest(BuildProfileResolveRequest{
      .profile_id = "desktop_full",
      .expected_target_platform = std::string("linux-x86_64-workstation"),
  });
  const auto runtime_result = provider.load_snapshot(RuntimePolicyLoadRequest{
      .profile_id = "desktop_full",
  });
    const auto minimal_runtime_result = provider.load_snapshot(RuntimePolicyLoadRequest{
      .profile_id = "edge_minimal",
    });

  assert_true(manifest_result.ok(), "build resolver should succeed for baseline desktop_full profile");
  assert_true(runtime_result.ok(), "runtime provider should succeed for baseline desktop_full profile");
    assert_true(minimal_runtime_result.ok(),
          "runtime provider should succeed for constrained edge_minimal profile");
  assert_equal(std::string("desktop_full"), runtime_result.snapshot->effective_profile_id(),
               "runtime snapshot should preserve the same profile id used by the build manifest");
  assert_true(manifest_result.manifest->enables_module("runtime"),
              "build manifest should keep runtime module enabled for the shared profile smoke path");
  assert_equal(std::string("prom_text"),
               runtime_result.snapshot->ops_policy().optional_backends.metrics_exporter_type,
               "runtime snapshot should project the profile-gated metrics exporter type instead of silently collapsing to noop");
    assert_equal(std::string("file"),
           runtime_result.snapshot->ops_policy().optional_backends.trace_exporter_type,
           "desktop_full should keep the profile-selected trace exporter type in the runtime snapshot");
    assert_equal(std::string("builtin:trace-file"),
           runtime_result.snapshot->ops_policy().optional_backends.trace_exporter_package_asset,
           "desktop_full should keep the trace package asset binding alongside exporter type");
    assert_equal(std::string("file"),
           runtime_result.snapshot->ops_policy().optional_backends.secret_backend_type,
           "desktop_full should surface the selected secret backend type in the runtime snapshot");
    assert_equal(std::string("noop"),
           minimal_runtime_result.snapshot->ops_policy().optional_backends.metrics_exporter_type,
           "edge_minimal should keep metrics exporter gated to noop in the constrained baseline");
    assert_equal(std::string("noop"),
           minimal_runtime_result.snapshot->ops_policy().optional_backends.trace_exporter_type,
           "edge_minimal should keep tracing exporter gated to noop in the constrained baseline");
}

void test_profiles_build_and_runtime_paths_reject_unknown_profile() {
  using dasall::profiles::BuildProfileResolveRequest;
  using dasall::profiles::BuildProfileResolver;
  using dasall::profiles::ProfileCatalog;
  using dasall::profiles::ProfileErrorCode;
  using dasall::profiles::RuntimePolicyLoadRequest;
  using dasall::profiles::RuntimePolicyProvider;
  using dasall::tests::support::assert_true;

  const ProfileCatalog catalog(repository_root() / "profiles");
  const BuildProfileResolver resolver(catalog);
  const RuntimePolicyProvider provider(catalog);

  const auto manifest_result = resolver.resolve_build_manifest(BuildProfileResolveRequest{
      .profile_id = "missing-profile",
      .expected_target_platform = std::nullopt,
  });
  const auto runtime_result = provider.load_snapshot(RuntimePolicyLoadRequest{
      .profile_id = "missing-profile",
  });

  assert_true(!manifest_result.ok(), "build resolver should reject unknown profiles in integration smoke");
  assert_true(manifest_result.error_code.has_value(),
              "build resolver should report an error code for unknown profile");
  assert_true(*manifest_result.error_code == ProfileErrorCode::ProfileNotFound,
              "build resolver should keep unknown-profile semantics stable");
  assert_true(!runtime_result.ok(), "runtime provider should reject unknown profiles in integration smoke");
  assert_true(runtime_result.error_code.has_value(),
              "runtime provider should report an error code for unknown profile");
  assert_true(*runtime_result.error_code == ProfileErrorCode::LastKnownGoodUnavailable,
              "runtime provider should preserve lkg-unavailable semantics when no fallback exists");
}

}  // namespace

int main() {
  try {
    test_profiles_build_and_runtime_paths_resolve_same_profile();
    test_profiles_build_and_runtime_paths_reject_unknown_profile();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}