#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "BuildProfileResolver.h"
#include "ProfileCatalog.h"
#include "ProfileCompatibilityValidator.h"
#include "ProfileError.h"
#include "RuntimePolicyProvider.h"
#include "ProfileYamlParser.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

[[nodiscard]] std::filesystem::path repository_root() {
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

[[nodiscard]] std::vector<std::string> extract_enabled_modules(
    const dasall::profiles::ParsedProfileYaml& parsed_yaml) {
  std::vector<std::string> enabled_modules;
  constexpr std::string_view kPrefix = "enabled_modules.";

  for (const auto& entry : parsed_yaml.scalar_values) {
    if (!entry.first.starts_with(kPrefix) || entry.second != "true") {
      continue;
    }

    enabled_modules.push_back(entry.first.substr(kPrefix.size()));
  }

  std::sort(enabled_modules.begin(), enabled_modules.end());
  return enabled_modules;
}

[[nodiscard]] std::vector<std::string> extract_enabled_adapters(
    const std::vector<std::string>& enabled_modules) {
  std::vector<std::string> enabled_adapters;

  for (const std::string& module_name : enabled_modules) {
    if (module_name.ends_with("_adapter")) {
      enabled_adapters.push_back(module_name);
    }
  }

  std::sort(enabled_adapters.begin(), enabled_adapters.end());
  return enabled_adapters;
}

void test_resolver_and_runtime_assets_stay_matrix_consistent_for_all_profiles() {
  using dasall::profiles::BuildProfileResolveRequest;
  using dasall::profiles::BuildProfileResolver;
  using dasall::profiles::ParsedProfileYaml;
  using dasall::profiles::ProfileCatalog;
  using dasall::profiles::ProfileCompatibilityState;
  using dasall::profiles::ProfileCompatibilityValidator;
  using dasall::profiles::ProfileRuntimeEnvironment;
  using dasall::profiles::RuntimePolicyProvider;
  using dasall::profiles::parse_profile_yaml_file;
  using dasall::tests::support::assert_true;

  const ProfileCatalog catalog(repository_root() / "profiles");
  const auto listed = catalog.list_profiles();
  assert_true(listed.ok(), "precondition: profile catalog should discover baseline profiles");
  assert_true(listed.has_consistent_values(),
              "precondition: discovered baseline profiles should have unique ids and complete metadata");

  const BuildProfileResolver resolver(catalog);
  const RuntimePolicyProvider provider(catalog);
  const ProfileCompatibilityValidator validator;

  for (const auto& descriptor : listed.profiles) {
    const auto manifest_result = resolver.resolve_build_manifest(BuildProfileResolveRequest{
        .profile_id = descriptor.profile_id,
        .expected_target_platform = descriptor.target_platform,
    });
    assert_true(manifest_result.ok(), "resolver should produce build manifest for each frozen profile");

    const auto runtime_result = provider.load_snapshot({.profile_id = descriptor.profile_id});
    assert_true(runtime_result.ok(), "runtime provider should load baseline snapshot for each frozen profile");

    const ParsedProfileYaml parsed_yaml = parse_profile_yaml_file(descriptor.asset_paths.runtime_policy_path);
    assert_true(parsed_yaml.ok, "runtime policy asset should parse when checking matrix consistency");

    const auto enabled_modules = extract_enabled_modules(parsed_yaml);
    const auto enabled_adapters = extract_enabled_adapters(enabled_modules);

    assert_true(enabled_modules == manifest_result.manifest->enabled_modules,
                "build manifest should preserve the same enabled_modules set as runtime policy asset");
    assert_true(enabled_adapters == manifest_result.manifest->enabled_adapters,
                "build manifest should derive adapter subset from the same runtime policy module matrix");

    const auto report = validator.validate(
        *runtime_result.snapshot,
        *manifest_result.manifest,
        ProfileRuntimeEnvironment{
            .target_platform = descriptor.target_platform,
            .available_modules = enabled_modules,
            .available_adapters = enabled_adapters,
        });

    assert_true(report.can_activate(), "matching build/runtime matrix should stay activatable");
    assert_true(report.compatibility_state == ProfileCompatibilityState::Compatible ||
                    report.compatibility_state == ProfileCompatibilityState::Warning,
                "matching matrix should not be downgraded to blocked state");
  }
}

void test_validator_rejects_runtime_environment_when_required_module_matrix_drifts() {
  using dasall::profiles::BuildProfileResolveRequest;
  using dasall::profiles::BuildProfileResolver;
  using dasall::profiles::ProfileCatalog;
  using dasall::profiles::ProfileCompatibilityState;
  using dasall::profiles::ProfileCompatibilityValidator;
  using dasall::profiles::ProfileErrorCode;
  using dasall::profiles::ProfileRuntimeEnvironment;
  using dasall::profiles::RuntimePolicyProvider;
  using dasall::tests::support::assert_true;

  const ProfileCatalog catalog(repository_root() / "profiles");
  const BuildProfileResolver resolver(catalog);
  const RuntimePolicyProvider provider(catalog);
  const ProfileCompatibilityValidator validator;

  const auto manifest_result = resolver.resolve_build_manifest(BuildProfileResolveRequest{
      .profile_id = "desktop_full",
      .expected_target_platform = std::string("linux-x86_64-workstation"),
  });
  const auto runtime_result = provider.load_snapshot({.profile_id = "desktop_full"});

  assert_true(manifest_result.ok(), "precondition: desktop_full manifest should resolve");
  assert_true(runtime_result.ok(), "precondition: desktop_full snapshot should load");

  const auto report = validator.validate(
      *runtime_result.snapshot,
      *manifest_result.manifest,
      ProfileRuntimeEnvironment{
          .target_platform = "linux-x86_64-workstation",
          .available_modules = {"cognition", "infra_observability", "llm_cloud_adapter"},
          .available_adapters = {"llm_cloud_adapter"},
      });

  assert_true(!report.can_activate(), "validator should reject runtime environment when required modules drift");
  assert_true(report.compatibility_state == ProfileCompatibilityState::Blocked,
              "required module drift should map to blocked state");
  assert_true(std::find(report.blocking_errors.begin(), report.blocking_errors.end(),
                        ProfileErrorCode::ModuleIncompatible) != report.blocking_errors.end(),
              "required module drift should surface module-incompatible error code");
  assert_true(std::find(report.dependency_gaps.begin(), report.dependency_gaps.end(),
                        std::string("required-module-missing:runtime")) != report.dependency_gaps.end(),
              "required module drift should record the missing module in dependency gaps");
}

}  // namespace

int main() {
  try {
    test_resolver_and_runtime_assets_stay_matrix_consistent_for_all_profiles();
    test_validator_rejects_runtime_environment_when_required_module_matrix_drifts();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}