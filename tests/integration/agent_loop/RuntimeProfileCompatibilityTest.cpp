#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "BuildProfileResolver.h"
#include "ProfileCatalog.h"
#include "ProfileCompatibilityValidator.h"
#include "RuntimePolicyProvider.h"
#include "support/TestAssertions.h"

namespace {

using dasall::profiles::BuildProfileManifest;
using dasall::profiles::BuildProfileResolveRequest;
using dasall::profiles::BuildProfileResolver;
using dasall::profiles::ProfileCatalog;
using dasall::profiles::ProfileCompatibilityState;
using dasall::profiles::ProfileCompatibilityValidator;
using dasall::profiles::ProfileRuntimeEnvironment;
using dasall::profiles::RuntimePolicyLoadRequest;
using dasall::profiles::RuntimePolicyProvider;
using dasall::profiles::RuntimePolicySnapshot;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

struct LoadedRuntimeProfile {
  std::string profile_id;
  std::string target_platform;
  BuildProfileManifest manifest;
  std::shared_ptr<const RuntimePolicySnapshot> snapshot;
};

[[nodiscard]] std::filesystem::path repository_root() {
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

template <typename T>
[[nodiscard]] T require_value(const std::optional<T>& value, const std::string& message) {
  assert_true(value.has_value(), message);
  return *value;
}

[[nodiscard]] LoadedRuntimeProfile load_profile(const std::string& profile_id,
                                                const std::string& expected_target_platform) {
  const ProfileCatalog catalog(repository_root() / "profiles");
  const BuildProfileResolver resolver(catalog);
  const RuntimePolicyProvider provider(catalog);

  const auto manifest_result = resolver.resolve_build_manifest(BuildProfileResolveRequest{
      .profile_id = profile_id,
      .expected_target_platform = expected_target_platform,
  });
  assert_true(manifest_result.ok(),
              "runtime profile compatibility should resolve build manifest for " + profile_id);

  const auto runtime_result = provider.load_snapshot(RuntimePolicyLoadRequest{
      .profile_id = profile_id,
  });
  assert_true(runtime_result.ok(),
              "runtime profile compatibility should load runtime snapshot for " + profile_id);
  assert_true(runtime_result.snapshot->has_consistent_values(),
              "runtime profile compatibility should keep the snapshot internally consistent for " +
                  profile_id);

  return LoadedRuntimeProfile{
      .profile_id = profile_id,
      .target_platform = expected_target_platform,
      .manifest = *manifest_result.manifest,
      .snapshot = runtime_result.snapshot,
  };
}

void test_runtime_profile_projection_tracks_budget_degrade_and_enablement_matrix() {
  const auto desktop = load_profile("desktop_full", "linux-x86_64-workstation");
  const auto cloud = load_profile("cloud_full", "linux-x86_64-server");
  const auto edge_balanced = load_profile("edge_balanced", "linux-arm64-embedded");
  const auto edge_minimal = load_profile("edge_minimal", "linux-arm64-embedded");

  assert_equal(std::string("desktop_full"), desktop.snapshot->effective_profile_id(),
               "runtime profile compatibility should preserve desktop_full as effective profile id");
  assert_equal(std::string("cloud_full"), cloud.snapshot->effective_profile_id(),
               "runtime profile compatibility should preserve cloud_full as effective profile id");
  assert_equal(std::string("edge_balanced"), edge_balanced.snapshot->effective_profile_id(),
               "runtime profile compatibility should preserve edge_balanced as effective profile id");
  assert_equal(std::string("edge_minimal"), edge_minimal.snapshot->effective_profile_id(),
               "runtime profile compatibility should preserve edge_minimal as effective profile id");

  assert_true(desktop.manifest.enables_module("llm_cloud_adapter"),
              "desktop_full should keep llm_cloud_adapter enabled in the build manifest");
  assert_true(edge_balanced.manifest.enables_module("llm_cloud_adapter"),
              "edge_balanced should keep llm_cloud_adapter enabled in the build manifest");
  assert_true(!edge_minimal.manifest.enables_module("llm_cloud_adapter"),
              "edge_minimal should disable llm_cloud_adapter in the build manifest");
  assert_true(desktop.manifest.enables_module("tools_mcp"),
              "desktop_full should keep tools_mcp enabled in the build manifest");
  assert_true(edge_balanced.manifest.enables_module("tools_mcp"),
              "edge_balanced should keep tools_mcp enabled in the build manifest");
  assert_true(!edge_minimal.manifest.enables_module("tools_mcp"),
              "edge_minimal should disable tools_mcp in the build manifest");
  assert_true(!desktop.manifest.enables_module("multi_agent"),
              "desktop_full should keep multi_agent disabled until a runtime coordinator is wired");
  assert_true(!cloud.manifest.enables_module("multi_agent"),
              "cloud_full should keep multi_agent disabled until a runtime coordinator is wired");
  assert_true(!edge_balanced.manifest.enables_module("multi_agent"),
              "edge_balanced should disable multi_agent in the build manifest");
  assert_true(!edge_minimal.manifest.enables_module("multi_agent"),
              "edge_minimal should keep multi_agent disabled in the build manifest");

  assert_equal(12U, desktop.snapshot->worker_threads(),
               "desktop_full should project the largest worker thread pool");
  assert_equal(6U, edge_balanced.snapshot->worker_threads(),
               "edge_balanced should project the mid-tier worker thread pool");
  assert_equal(2U, edge_minimal.snapshot->worker_threads(),
               "edge_minimal should project the constrained worker thread pool");

  const auto& desktop_budget = desktop.snapshot->runtime_budget();
  const auto& edge_balanced_budget = edge_balanced.snapshot->runtime_budget();
  const auto& edge_minimal_budget = edge_minimal.snapshot->runtime_budget();

  assert_equal(64000U,
               require_value(desktop_budget.max_tokens,
                             "desktop_full should expose max_tokens in RuntimeBudget"),
               "desktop_full should project the broadest token budget");
  assert_equal(24000U,
               require_value(edge_balanced_budget.max_tokens,
                             "edge_balanced should expose max_tokens in RuntimeBudget"),
               "edge_balanced should project the mid-tier token budget");
  assert_equal(8000U,
               require_value(edge_minimal_budget.max_tokens,
                             "edge_minimal should expose max_tokens in RuntimeBudget"),
               "edge_minimal should project the most constrained token budget");
  assert_equal(24U,
               require_value(desktop_budget.max_tool_calls,
                             "desktop_full should expose max_tool_calls in RuntimeBudget"),
               "desktop_full should project the broadest tool-call budget");
  assert_equal(10U,
               require_value(edge_balanced_budget.max_tool_calls,
                             "edge_balanced should expose max_tool_calls in RuntimeBudget"),
               "edge_balanced should project the mid-tier tool-call budget");
  assert_equal(4U,
               require_value(edge_minimal_budget.max_tool_calls,
                             "edge_minimal should expose max_tool_calls in RuntimeBudget"),
               "edge_minimal should project the narrowest tool-call budget");
  assert_equal(45000U,
               require_value(desktop_budget.max_latency_ms,
                             "desktop_full should expose max_latency_ms in RuntimeBudget"),
               "desktop_full should keep the broadest latency budget");

  assert_equal(7000U,
               require_value(edge_balanced_budget.max_latency_ms,
                             "edge_balanced should expose max_latency_ms in RuntimeBudget"),
               "edge_balanced should keep the mid-tier latency budget");
  assert_equal(5000U,
               require_value(edge_minimal_budget.max_latency_ms,
                             "edge_minimal should expose max_latency_ms in RuntimeBudget"),
               "edge_minimal should keep the constrained latency budget");

  assert_equal(18U, desktop.snapshot->token_budget_policy().max_history_turns,
               "desktop_full should keep the broadest history turn window");
  assert_equal(10U, edge_balanced.snapshot->token_budget_policy().max_history_turns,
               "edge_balanced should keep the mid-tier history turn window");
  assert_equal(4U, edge_minimal.snapshot->token_budget_policy().max_history_turns,
               "edge_minimal should keep the floor history turn window");
  assert_equal(12000U, desktop.snapshot->token_budget_policy().compression_threshold,
               "desktop_full should keep the broadest compression threshold");
  assert_equal(6000U, edge_balanced.snapshot->token_budget_policy().compression_threshold,
               "edge_balanced should keep the mid-tier compression threshold");
  assert_equal(2000U, edge_minimal.snapshot->token_budget_policy().compression_threshold,
               "edge_minimal should keep the smallest compression threshold");

  assert_equal(std::string("cloud.reasoning"),
               desktop.snapshot->model_profile().stage_routes.at("planning").route,
               "desktop_full should route planning traffic to cloud.reasoning");
  assert_equal(std::string("cloud.reasoning"),
               desktop.snapshot->model_profile().stage_routes.at("execution").route,
               "desktop_full should route execution traffic to cloud.reasoning");
  assert_equal(std::string("cloud.reasoning"),
               desktop.snapshot->model_profile().stage_routes.at("reflection").route,
               "desktop_full should route reflection traffic to cloud.reasoning");
  assert_equal(std::string("cloud.general"),
               desktop.snapshot->model_profile().stage_routes.at("response").route,
               "desktop_full should route response traffic to cloud.general");
  assert_equal(std::string("lan.general"),
               edge_balanced.snapshot->model_profile().stage_routes.at("planning").route,
               "edge_balanced should route planning traffic to lan.general");
  assert_equal(std::string("lan.general"),
               edge_balanced.snapshot->model_profile().stage_routes.at("execution").route,
               "edge_balanced should route execution traffic to lan.general");
  assert_equal(std::string("lan.general"),
               edge_balanced.snapshot->model_profile().stage_routes.at("reflection").route,
               "edge_balanced should route reflection traffic to lan.general");
  assert_equal(std::string("lan.general"),
               edge_balanced.snapshot->model_profile().stage_routes.at("response").route,
               "edge_balanced should route response traffic to lan.general");
  assert_equal(std::string("local.small"),
               edge_minimal.snapshot->model_profile().stage_routes.at("planning").route,
               "edge_minimal should route planning traffic to local.small");
  assert_equal(std::string("local.small"),
               edge_minimal.snapshot->model_profile().stage_routes.at("execution").route,
               "edge_minimal should route execution traffic to local.small");
  assert_equal(std::string("local.small"),
               edge_minimal.snapshot->model_profile().stage_routes.at("reflection").route,
               "edge_minimal should route reflection traffic to local.small");
  assert_equal(std::string("local.small"),
               edge_minimal.snapshot->model_profile().stage_routes.at("response").route,
               "edge_minimal should route response traffic to local.small");

  assert_true(!desktop.snapshot->capability_cache_policy().stale_read_allowed,
              "desktop_full should keep stale capability reads disabled");
  assert_true(edge_balanced.snapshot->capability_cache_policy().stale_read_allowed,
              "edge_balanced should enable stale capability reads");
  assert_true(edge_minimal.snapshot->capability_cache_policy().stale_read_allowed,
              "edge_minimal should keep stale capability reads enabled");

  assert_true(desktop.snapshot->degrade_policy().allow_model_failover,
              "desktop_full should allow model failover");
  assert_true(edge_balanced.snapshot->degrade_policy().allow_model_failover,
              "edge_balanced should allow model failover");
  assert_true(!edge_minimal.snapshot->degrade_policy().allow_model_failover,
              "edge_minimal should disable model failover");
  assert_equal(std::string("lan.general"), desktop.snapshot->degrade_policy().fallback_chain.front(),
               "desktop_full should prefer lan.general as the first degrade fallback");
  assert_equal(std::string("local.small"), edge_balanced.snapshot->degrade_policy().fallback_chain.front(),
               "edge_balanced should prefer local.small as the first degrade fallback");
  assert_equal(std::string("builtin_only"), edge_minimal.snapshot->degrade_policy().fallback_chain.front(),
               "edge_minimal should fall back directly to builtin_only");

  assert_equal(std::string("full"), desktop.snapshot->execution_policy().audit_level,
               "desktop_full should keep the fullest audit level");
  assert_equal(std::string("standard"), edge_balanced.snapshot->execution_policy().audit_level,
               "edge_balanced should keep the mid-tier audit level");
  assert_equal(std::string("minimal"), edge_minimal.snapshot->execution_policy().audit_level,
               "edge_minimal should keep the constrained audit level");
  assert_equal(std::size_t{3}, desktop.snapshot->execution_policy().allowed_tool_domains.size(),
               "desktop_full should allow builtin, mcp and workflow tool domains");
  assert_equal(std::size_t{2}, edge_balanced.snapshot->execution_policy().allowed_tool_domains.size(),
               "edge_balanced should allow builtin and mcp tool domains");
  assert_equal(std::size_t{1}, edge_minimal.snapshot->execution_policy().allowed_tool_domains.size(),
               "edge_minimal should only allow builtin tool domains");

  assert_true(desktop.snapshot->ops_policy().remote_diagnostics_enabled,
              "desktop_full should keep remote diagnostics enabled");
  assert_true(edge_balanced.snapshot->ops_policy().remote_diagnostics_enabled,
              "edge_balanced should keep remote diagnostics enabled");
  assert_true(!edge_minimal.snapshot->ops_policy().remote_diagnostics_enabled,
              "edge_minimal should disable remote diagnostics");
}

void test_runtime_profile_projection_stays_compatible_with_shared_validator() {
  const std::vector<LoadedRuntimeProfile> profiles{
      load_profile("desktop_full", "linux-x86_64-workstation"),
      load_profile("edge_balanced", "linux-arm64-embedded"),
      load_profile("edge_minimal", "linux-arm64-embedded"),
  };
  const ProfileCompatibilityValidator validator;

  for (const auto& profile : profiles) {
    const auto report = validator.validate(
        *profile.snapshot,
        profile.manifest,
        ProfileRuntimeEnvironment{
            .target_platform = profile.target_platform,
            .available_modules = profile.manifest.enabled_modules,
            .available_adapters = profile.manifest.enabled_adapters,
        });
    assert_true(report.can_activate(),
                "runtime profile compatibility should remain activatable for " +
                    profile.profile_id);
    assert_true(report.compatibility_state == ProfileCompatibilityState::Compatible,
                "runtime profile compatibility should keep the shared validator in Compatible for " +
                    profile.profile_id);
  }
}

void test_runtime_profile_compatibility_test_is_registered_for_discoverability() {
  const auto integration_cmake = read_text_file(
      repository_root() / "tests" / "integration" / "agent_loop" / "CMakeLists.txt");

  assert_true(integration_cmake.find("RuntimeProfileCompatibilityTest") != std::string::npos,
              "runtime agent-loop integration CMake should register RuntimeProfileCompatibilityTest");
  assert_true(integration_cmake.find("dasall_runtime_profile_compatibility_integration_test") !=
                  std::string::npos,
              "runtime agent-loop integration CMake should expose a namespaced runtime profile compatibility target");
}

}  // namespace

int main() {
  try {
    test_runtime_profile_projection_tracks_budget_degrade_and_enablement_matrix();
    test_runtime_profile_projection_stays_compatible_with_shared_validator();
    test_runtime_profile_compatibility_test_is_registered_for_discoverability();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}