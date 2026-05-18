#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "BuildProfileResolver.h"
#include "ProfileCatalog.h"
#include "ProfileCompatibilityValidator.h"
#include "RuntimePolicyProvider.h"
#include "config/KnowledgeConfigProjector.h"
#include "support/TestAssertions.h"

#ifndef DASALL_KNOWLEDGE_INTEGRATION_TEST_DIR
#define DASALL_KNOWLEDGE_INTEGRATION_TEST_DIR "/home/gangan/DASALL/tests/integration/knowledge"
#endif

namespace {

using dasall::knowledge::KnowledgeConfigSnapshot;
using dasall::knowledge::RetrievalMode;
using dasall::knowledge::config::KnowledgeConfigProjector;
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

struct LoadedKnowledgeProfile {
  std::string profile_id;
  std::string target_platform;
  BuildProfileManifest manifest;
  std::shared_ptr<const RuntimePolicySnapshot> snapshot;
  KnowledgeConfigSnapshot config;
};

[[nodiscard]] std::filesystem::path repository_root() {
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

[[nodiscard]] LoadedKnowledgeProfile load_profile(const std::string& profile_id,
                                                  const std::string& expected_target_platform) {
  const ProfileCatalog catalog(repository_root() / "profiles");
  const BuildProfileResolver resolver(catalog);
  const RuntimePolicyProvider provider(catalog);

  const auto manifest_result = resolver.resolve_build_manifest(BuildProfileResolveRequest{
      .profile_id = profile_id,
      .expected_target_platform = expected_target_platform,
  });
  assert_true(manifest_result.ok(),
              "knowledge profile compatibility should resolve build manifest for " + profile_id);

  const auto runtime_result = provider.load_snapshot(RuntimePolicyLoadRequest{
      .profile_id = profile_id,
  });
  assert_true(runtime_result.ok(),
              "knowledge profile compatibility should load runtime snapshot for " + profile_id);

  const auto config = KnowledgeConfigProjector::project(*runtime_result.snapshot,
                                                        *manifest_result.manifest);
  assert_true(config.has_value(),
              "knowledge profile compatibility should project KnowledgeConfigSnapshot for " +
                  profile_id);
  assert_true(config->has_consistent_values(),
              "knowledge profile compatibility should keep the projected config consistent for " +
                  profile_id);

  return LoadedKnowledgeProfile{
      .profile_id = profile_id,
      .target_platform = expected_target_platform,
      .manifest = *manifest_result.manifest,
      .snapshot = runtime_result.snapshot,
      .config = *config,
  };
}

[[nodiscard]] BuildProfileManifest without_module(const BuildProfileManifest& manifest,
                                                  std::string_view module_name) {
  BuildProfileManifest downgraded = manifest;
  downgraded.enabled_modules.erase(
      std::remove(downgraded.enabled_modules.begin(),
                  downgraded.enabled_modules.end(),
                  std::string(module_name)),
      downgraded.enabled_modules.end());
  downgraded.enabled_adapters.erase(
      std::remove(downgraded.enabled_adapters.begin(),
                  downgraded.enabled_adapters.end(),
                  std::string(module_name)),
      downgraded.enabled_adapters.end());
  return downgraded;
}

void assert_vector_capable_lexical_default(const LoadedKnowledgeProfile& profile,
                                           const std::string& message_prefix) {
  assert_true(profile.config.knowledge_enabled,
              message_prefix + " should keep knowledge enabled");
  assert_true(profile.config.vector_enabled,
              message_prefix + " should keep vector retrieval enabled");
  assert_true(profile.config.retrieval_mode_default == RetrievalMode::LexicalOnly,
              message_prefix + " should keep lexical-only as the production default retrieval mode");
}

void assert_lexical_only_mode(const LoadedKnowledgeProfile& profile,
                              bool knowledge_enabled,
                              bool vector_enabled,
                              const std::string& message_prefix) {
  assert_true(profile.config.knowledge_enabled == knowledge_enabled,
              message_prefix + " should keep the expected knowledge enabled bit");
  assert_true(profile.config.vector_enabled == vector_enabled,
              message_prefix + " should keep the expected vector enabled bit");
  assert_true(profile.config.retrieval_mode_default == RetrievalMode::LexicalOnly,
              message_prefix + " should project lexical-only as the default retrieval mode");
}

void test_knowledge_profile_projection_tracks_real_profile_matrix() {
  const auto desktop = load_profile("desktop_full", "linux-x86_64-workstation");
  const auto cloud = load_profile("cloud_full", "linux-x86_64-server");
  const auto edge_balanced = load_profile("edge_balanced", "linux-arm64-embedded");
  const auto edge_minimal = load_profile("edge_minimal", "linux-arm64-embedded");
  const auto factory = load_profile("factory_test", "linux-arm64-factory");

  assert_equal(std::string("desktop_full"), desktop.snapshot->effective_profile_id(),
               "knowledge profile compatibility should preserve desktop_full as effective profile id");
  assert_equal(std::string("cloud_full"), cloud.snapshot->effective_profile_id(),
               "knowledge profile compatibility should preserve cloud_full as effective profile id");
  assert_equal(std::string("edge_balanced"), edge_balanced.snapshot->effective_profile_id(),
               "knowledge profile compatibility should preserve edge_balanced as effective profile id");
  assert_equal(std::string("edge_minimal"), edge_minimal.snapshot->effective_profile_id(),
               "knowledge profile compatibility should preserve edge_minimal as effective profile id");
  assert_equal(std::string("factory_test"), factory.snapshot->effective_profile_id(),
               "knowledge profile compatibility should preserve factory_test as effective profile id");

  assert_true(desktop.manifest.enables_module("knowledge"),
              "desktop_full build manifest should enable knowledge");
  assert_true(cloud.manifest.enables_module("knowledge"),
              "cloud_full build manifest should enable knowledge");
  assert_true(edge_balanced.manifest.enables_module("knowledge"),
              "edge_balanced build manifest should enable knowledge");
  assert_true(!edge_minimal.manifest.enables_module("knowledge"),
              "edge_minimal build manifest should disable knowledge by default");
  assert_true(!factory.manifest.enables_module("knowledge"),
              "factory_test build manifest should disable knowledge by default");

  assert_vector_capable_lexical_default(desktop, "desktop_full");
  assert_vector_capable_lexical_default(cloud, "cloud_full");
  assert_vector_capable_lexical_default(edge_balanced, "edge_balanced");
  assert_lexical_only_mode(edge_minimal, false, false, "edge_minimal");
  assert_lexical_only_mode(factory, false, false, "factory_test");

  assert_equal(std::size_t{4000}, desktop.config.evidence_budget_tokens,
               "desktop_full should derive evidence budget from runtime token policy");
  assert_equal(std::size_t{5000}, cloud.config.evidence_budget_tokens,
               "cloud_full should derive the largest evidence budget from runtime token policy");
  assert_equal(std::size_t{2000}, edge_balanced.config.evidence_budget_tokens,
               "edge_balanced should tighten evidence budget relative to desktop_full");
  assert_equal(std::size_t{750}, edge_minimal.config.evidence_budget_tokens,
               "edge_minimal should keep a reduced lexical evidence budget even with knowledge disabled");
  assert_equal(std::size_t{1000}, factory.config.evidence_budget_tokens,
               "factory_test should derive a reduced diagnostic evidence budget");

  assert_equal(std::size_t{8}, desktop.config.max_context_projection_items,
               "desktop_full should project the broadest context projection window");
  assert_equal(std::size_t{8}, cloud.config.max_context_projection_items,
               "cloud_full should project the broadest context projection window");
  assert_equal(std::size_t{6}, edge_balanced.config.max_context_projection_items,
               "edge_balanced should project the mid-tier context projection window");
  assert_equal(std::size_t{4}, edge_minimal.config.max_context_projection_items,
               "edge_minimal should project the floor context projection window");
  assert_equal(std::size_t{6}, factory.config.max_context_projection_items,
               "factory_test should project the mid-tier context projection window");

  assert_equal(std::int64_t{1500}, desktop.config.request_deadline_ms,
               "desktop_full should clamp request deadline to the shared upper bound");
  assert_equal(std::int64_t{1500}, cloud.config.request_deadline_ms,
               "cloud_full should clamp request deadline to the shared upper bound");
  assert_equal(std::int64_t{1500}, edge_balanced.config.request_deadline_ms,
               "edge_balanced should clamp request deadline to the shared upper bound");
  assert_equal(std::int64_t{1500}, edge_minimal.config.request_deadline_ms,
               "edge_minimal should clamp request deadline to the shared upper bound");
  assert_equal(std::int64_t{1500}, factory.config.request_deadline_ms,
               "factory_test should clamp request deadline to the shared upper bound");

  assert_equal(std::size_t{2}, desktop.config.max_parallel_recall,
               "desktop_full should allow two parallel recall lanes");
  assert_equal(std::size_t{2}, cloud.config.max_parallel_recall,
               "cloud_full should allow two parallel recall lanes");
  assert_equal(std::size_t{2}, edge_balanced.config.max_parallel_recall,
               "edge_balanced should still allow two parallel recall lanes");
  assert_equal(std::size_t{1}, edge_minimal.config.max_parallel_recall,
               "edge_minimal should collapse recall parallelism to one");
  assert_equal(std::size_t{2}, factory.config.max_parallel_recall,
               "factory_test should allow two parallel recall lanes");

  assert_equal(std::int64_t{525}, desktop.config.sparse_recall_timeout_ms,
               "desktop_full should derive sparse recall timeout from the shared request deadline");
  assert_equal(std::int64_t{525}, desktop.config.dense_recall_timeout_ms,
               "desktop_full should derive dense recall timeout from the shared request deadline");
  assert_equal(std::int64_t{525}, edge_minimal.config.sparse_recall_timeout_ms,
               "edge_minimal should keep the derived sparse lane timeout even when knowledge is disabled");
  assert_equal(std::int64_t{525}, factory.config.dense_recall_timeout_ms,
               "factory_test should keep the derived dense lane timeout stable");

  assert_equal(std::int64_t{30000}, desktop.config.ingest_timeout_ms,
               "desktop_full should use the non-edge ingest timeout");
  assert_equal(std::int64_t{30000}, cloud.config.ingest_timeout_ms,
               "cloud_full should use the non-edge ingest timeout");
  assert_equal(std::int64_t{10000}, edge_balanced.config.ingest_timeout_ms,
               "edge_balanced should use the edge-like ingest timeout");
  assert_equal(std::int64_t{10000}, edge_minimal.config.ingest_timeout_ms,
               "edge_minimal should use the edge-like ingest timeout");
  assert_equal(std::int64_t{10000}, factory.config.ingest_timeout_ms,
               "factory_test should use the edge-like ingest timeout");

  assert_true(!desktop.config.allow_stale_read,
              "desktop_full should keep stale reads disabled");
  assert_true(!cloud.config.allow_stale_read,
              "cloud_full should keep stale reads disabled");
  assert_true(edge_balanced.config.allow_stale_read,
              "edge_balanced should enable stale reads");
  assert_true(edge_minimal.config.allow_stale_read,
              "edge_minimal should enable stale reads for constrained deployments");
  assert_true(factory.config.allow_stale_read,
              "factory_test should enable stale reads for diagnostic deployments");

  assert_true(desktop.config.allow_budget_degrade,
              "desktop_full should keep budget degrade enabled");
  assert_true(cloud.config.allow_budget_degrade,
              "cloud_full should keep budget degrade enabled");
  assert_true(edge_balanced.config.allow_budget_degrade,
              "edge_balanced should keep budget degrade enabled");
  assert_true(edge_minimal.config.allow_budget_degrade,
              "edge_minimal should keep budget degrade enabled");
  assert_true(factory.config.allow_budget_degrade,
              "factory_test should keep budget degrade enabled");

  assert_equal(std::int64_t{30000}, desktop.config.catalog_refresh_interval_ms,
               "desktop_full should preserve runtime cache refresh interval in the projected config");
  assert_equal(std::int64_t{20000}, cloud.config.catalog_refresh_interval_ms,
               "cloud_full should preserve runtime cache refresh interval in the projected config");
  assert_equal(std::int64_t{15000}, edge_balanced.config.catalog_refresh_interval_ms,
               "edge_balanced should preserve runtime cache refresh interval in the projected config");
  assert_equal(std::int64_t{10000}, edge_minimal.config.catalog_refresh_interval_ms,
               "edge_minimal should preserve runtime cache refresh interval in the projected config");
  assert_equal(std::int64_t{12000}, factory.config.catalog_refresh_interval_ms,
               "factory_test should preserve runtime cache refresh interval in the projected config");
}

void test_knowledge_profile_projection_accepts_knowledge_without_vector() {
  const auto desktop = load_profile("desktop_full", "linux-x86_64-workstation");
  const auto downgraded_manifest = without_module(desktop.manifest, "memory_vector");

  assert_true(downgraded_manifest.has_consistent_values(),
              "synthetic manifest downgrade should keep a structurally valid build manifest");
  assert_true(downgraded_manifest.enables_module("knowledge"),
              "synthetic manifest downgrade should keep knowledge enabled");
  assert_true(!downgraded_manifest.enables_module("memory_vector"),
              "synthetic manifest downgrade should remove memory_vector only");

  const auto downgraded_config =
      KnowledgeConfigProjector::project(*desktop.snapshot, downgraded_manifest);

  assert_true(downgraded_config.has_value(),
              "knowledge profile compatibility should accept knowledge without vector as a legal projection");
  assert_true(downgraded_config->has_consistent_values(),
              "knowledge profile compatibility should keep the downgraded projection structurally valid");
  assert_true(downgraded_config->knowledge_enabled,
              "knowledge profile compatibility should keep knowledge enabled after vector downgrade");
  assert_true(!downgraded_config->vector_enabled,
              "knowledge profile compatibility should disable vector after synthetic manifest downgrade");
  assert_true(downgraded_config->retrieval_mode_default == RetrievalMode::LexicalOnly,
              "knowledge profile compatibility should fall back to lexical-only when vector is disabled");
}

void test_knowledge_profiles_stay_compatible_with_shared_profile_validator() {
  const std::vector<LoadedKnowledgeProfile> profiles{
      load_profile("desktop_full", "linux-x86_64-workstation"),
      load_profile("cloud_full", "linux-x86_64-server"),
      load_profile("edge_balanced", "linux-arm64-embedded"),
      load_profile("edge_minimal", "linux-arm64-embedded"),
      load_profile("factory_test", "linux-arm64-factory"),
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
                "knowledge profile compatibility should remain activatable for " +
                    profile.profile_id);
    assert_true(report.compatibility_state == ProfileCompatibilityState::Compatible,
                "knowledge profile compatibility should remain in compatible state for " +
                    profile.profile_id);
  }
}

void test_knowledge_profile_compatibility_test_is_registered_for_discoverability() {
  const std::filesystem::path integration_cmake_path =
      std::filesystem::path(DASALL_KNOWLEDGE_INTEGRATION_TEST_DIR) / "CMakeLists.txt";
  const std::string integration_cmake = read_text_file(integration_cmake_path);

  assert_true(integration_cmake.find("KnowledgeProfileCompatibilityTest") != std::string::npos,
              "knowledge integration CMake should register KnowledgeProfileCompatibilityTest for discoverability");
  assert_true(integration_cmake.find(
                  "dasall_knowledge_profile_compatibility_integration_test") !=
                  std::string::npos,
              "knowledge integration CMake should expose a namespaced profile compatibility target");
}

}  // namespace

int main() {
  try {
    test_knowledge_profile_projection_tracks_real_profile_matrix();
    test_knowledge_profile_projection_accepts_knowledge_without_vector();
    test_knowledge_profiles_stay_compatible_with_shared_profile_validator();
    test_knowledge_profile_compatibility_test_is_registered_for_discoverability();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}