#include <exception>
#include <cstdint>
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
#include "config/MemoryConfigProjector.h"
#include "support/TestAssertions.h"

#ifndef DASALL_MEMORY_INTEGRATION_TEST_DIR
#define DASALL_MEMORY_INTEGRATION_TEST_DIR "/home/gangan/DASALL/tests/integration/memory"
#endif

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

struct LoadedMemoryProfile {
  std::string profile_id;
  std::string target_platform;
  dasall::profiles::BuildProfileManifest manifest;
  std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot> snapshot;
  dasall::memory::MemoryConfig config;
};

[[nodiscard]] std::filesystem::path repository_root() {
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

[[nodiscard]] LoadedMemoryProfile load_profile(
    const std::string& profile_id,
    const std::string& expected_target_platform) {
  const dasall::profiles::ProfileCatalog catalog(repository_root() / "profiles");
  const dasall::profiles::BuildProfileResolver resolver(catalog);
  const dasall::profiles::RuntimePolicyProvider provider(catalog);

  const auto manifest_result = resolver.resolve_build_manifest(
      dasall::profiles::BuildProfileResolveRequest{
          .profile_id = profile_id,
          .expected_target_platform = expected_target_platform,
      });
  assert_true(manifest_result.ok(),
              "memory profile integration should resolve the build manifest for " +
                  profile_id);

  const auto runtime_result = provider.load_snapshot(
      dasall::profiles::RuntimePolicyLoadRequest{.profile_id = profile_id});
  assert_true(runtime_result.ok(),
              "memory profile integration should load the runtime snapshot for " +
                  profile_id);

  const auto config = dasall::memory::config::project_memory_config(
      *runtime_result.snapshot,
      *manifest_result.manifest);
  assert_true(config.has_value(),
              "memory profile integration should project a MemoryConfig for " +
                  profile_id);

  return LoadedMemoryProfile{
      .profile_id = profile_id,
      .target_platform = expected_target_platform,
      .manifest = *manifest_result.manifest,
      .snapshot = runtime_result.snapshot,
      .config = *config,
  };
}

void test_memory_profile_projection_tracks_vector_maintenance_and_budget_differences() {
  const auto desktop = load_profile("desktop_full", "linux-x86_64-workstation");
  const auto edge_balanced = load_profile("edge_balanced", "linux-arm64-embedded");
  const auto edge_minimal = load_profile("edge_minimal", "linux-arm64-embedded");
  const auto cloud = load_profile("cloud_full", "linux-x86_64-server");
  const auto factory = load_profile("factory_test", "linux-arm64-factory");

  assert_equal(std::string("desktop_full"), desktop.snapshot->effective_profile_id(),
               "memory profile projection should preserve the desktop profile id");
  assert_equal(std::string("edge_balanced"), edge_balanced.snapshot->effective_profile_id(),
               "memory profile projection should preserve the edge_balanced profile id");
  assert_equal(std::string("edge_minimal"), edge_minimal.snapshot->effective_profile_id(),
               "memory profile projection should preserve the edge_minimal profile id");

  assert_true(desktop.manifest.enables_module("memory_vector"),
              "desktop_full should keep memory_vector enabled in the build manifest");
  assert_true(edge_balanced.manifest.enables_module("memory_vector"),
              "edge_balanced should keep memory_vector enabled in the build manifest");
  assert_true(!edge_minimal.manifest.enables_module("memory_vector"),
              "edge_minimal should disable memory_vector in the build manifest");

  assert_true(desktop.config.vector.enabled,
              "desktop_full should project vector as enabled");
  assert_true(edge_balanced.config.vector.enabled,
              "edge_balanced should project vector as enabled");
  assert_true(!edge_minimal.config.vector.enabled,
              "edge_minimal should project vector as disabled without breaking the rest of MemoryConfig");
  assert_equal(std::string("sqlite-vss"), std::string(dasall::memory::to_string_view(desktop.config.vector.backend_type)),
               "desktop_full should keep sqlite-vss as the projected vector backend");
  assert_equal(std::string("sqlite-vss"), std::string(dasall::memory::to_string_view(edge_balanced.config.vector.backend_type)),
               "edge_balanced should keep sqlite-vss as the projected vector backend");
  assert_equal(std::string("none"), std::string(dasall::memory::to_string_view(edge_minimal.config.vector.backend_type)),
               "edge_minimal should downgrade the projected vector backend to none");
  assert_equal(8, desktop.config.vector.search_top_k,
               "desktop_full should keep the broader vector search window");
  assert_equal(5, edge_balanced.config.vector.search_top_k,
               "edge_balanced should tighten the vector search window relative to desktop");
  assert_equal(0, edge_minimal.config.vector.search_top_k,
               "edge_minimal should fully disable vector search fanout");
  assert_equal(std::string("tiktoken"),
               std::string(dasall::memory::to_string_view(desktop.config.token_estimator)),
               "desktop_full should project tiktoken token estimation");
  assert_equal(std::string("tiktoken"),
               std::string(dasall::memory::to_string_view(edge_balanced.config.token_estimator)),
               "edge_balanced should project tiktoken token estimation");
  assert_equal(std::string("tiktoken"),
               std::string(dasall::memory::to_string_view(edge_minimal.config.token_estimator)),
               "edge_minimal should project tiktoken token estimation");

  assert_equal(18, desktop.config.context.recent_turn_limit,
               "desktop_full should project the broader history window from runtime policy");
  assert_equal(10, edge_balanced.config.context.recent_turn_limit,
               "edge_balanced should project the tighter history window from runtime policy");
  assert_equal(4, edge_minimal.config.context.recent_turn_limit,
               "edge_minimal should project the constrained history window from runtime policy");
  assert_equal(14, desktop.config.context.compression_trigger_turns,
               "desktop_full should delay compression until the larger history window is populated");
  assert_equal(8, edge_balanced.config.context.compression_trigger_turns,
               "edge_balanced should trigger compression earlier than desktop_full");
  assert_equal(4, edge_minimal.config.context.compression_trigger_turns,
               "edge_minimal should compress at the floor threshold to protect its small budget");
  assert_equal(3, desktop.config.context.max_summary_candidates,
               "desktop_full should keep the broader summary candidate fan-in");
  assert_equal(2, edge_balanced.config.context.max_summary_candidates,
               "edge_balanced should keep a tighter summary candidate fan-in");
  assert_equal(1, edge_minimal.config.context.max_summary_candidates,
               "edge_minimal should fall back to a single summary candidate");

  assert_true(desktop.config.storage.wal_autocheckpoint_pages >
                  edge_balanced.config.storage.wal_autocheckpoint_pages,
              "desktop_full should project a broader WAL autocheckpoint budget than edge_balanced");
  assert_true(edge_balanced.config.storage.wal_autocheckpoint_pages >
                  edge_minimal.config.storage.wal_autocheckpoint_pages,
              "edge_balanced should project a broader WAL autocheckpoint budget than edge_minimal");
  assert_equal(4, desktop.config.storage.reader_pool_size,
               "desktop_full should keep the largest reader pool");
  assert_equal(3, edge_balanced.config.storage.reader_pool_size,
               "edge_balanced should keep a mid-sized reader pool");
  assert_equal(1, edge_minimal.config.storage.reader_pool_size,
               "edge_minimal should collapse the reader pool to the minimal baseline");

  assert_true(desktop.config.maintenance.auto_schedule,
              "desktop_full should keep maintenance auto-schedule enabled");
  assert_true(edge_balanced.config.maintenance.auto_schedule,
              "edge_balanced should keep maintenance auto-schedule enabled");
  assert_true(!edge_minimal.config.maintenance.auto_schedule,
              "edge_minimal should disable maintenance auto-schedule by projection");
  assert_equal(std::int64_t{60000}, desktop.config.maintenance.schedule_interval_ms,
               "desktop_full should project the fastest maintenance cadence");
  assert_equal(std::int64_t{90000}, edge_balanced.config.maintenance.schedule_interval_ms,
               "edge_balanced should project the mid-tier maintenance cadence");
  assert_equal(std::int64_t{120000}, edge_minimal.config.maintenance.schedule_interval_ms,
               "edge_minimal should project the slowest maintenance cadence");
  assert_equal(std::int64_t{60000}, desktop.snapshot->memory_maintenance_policy().interval_ms,
               "desktop_full runtime snapshot should expose the explicit memory maintenance interval");
  assert_equal(std::int64_t{8000}, edge_balanced.snapshot->memory_maintenance_policy().jitter_ms,
               "edge_balanced runtime snapshot should expose the explicit memory maintenance jitter");
  assert_equal(std::string("passive_on_retention"),
               edge_minimal.snapshot->memory_maintenance_policy().checkpoint_strategy,
               "edge_minimal runtime snapshot should expose the reduced checkpoint strategy");

  // --- L13: cloud_full and factory_test profile coverage ---
  assert_equal(std::string("cloud_full"), cloud.snapshot->effective_profile_id(),
               "memory profile projection should preserve the cloud_full profile id");
  assert_equal(std::string("factory_test"), factory.snapshot->effective_profile_id(),
               "memory profile projection should preserve the factory_test profile id");

  assert_true(cloud.config.vector.enabled,
              "cloud_full should project vector as enabled");
  assert_equal(std::string("sqlite-vss"),
               std::string(dasall::memory::to_string_view(cloud.config.vector.backend_type)),
               "cloud_full should keep sqlite-vss as the projected vector backend");
  assert_equal(std::string("tiktoken"),
               std::string(dasall::memory::to_string_view(cloud.config.token_estimator)),
               "cloud_full should project tiktoken token estimation");
  assert_true(!factory.config.vector.enabled,
              "factory_test should project vector as disabled");
  assert_equal(std::string("none"),
               std::string(dasall::memory::to_string_view(factory.config.vector.backend_type)),
               "factory_test should downgrade the projected vector backend to none");
  assert_equal(std::string("tiktoken"),
               std::string(dasall::memory::to_string_view(factory.config.token_estimator)),
               "factory_test should project tiktoken token estimation");

  assert_true(cloud.config.maintenance.auto_schedule,
              "cloud_full should keep maintenance auto-schedule enabled");
  assert_true(factory.config.maintenance.auto_schedule,
              "factory_test should keep maintenance auto-schedule enabled with 4 worker threads");
  assert_equal(std::int64_t{90000}, factory.config.maintenance.schedule_interval_ms,
               "factory_test should project the explicit memory maintenance cadence from runtime policy");
}

void test_memory_profile_projection_stays_compatible_with_profile_validator() {
  const std::vector<LoadedMemoryProfile> profiles{
      load_profile("desktop_full", "linux-x86_64-workstation"),
      load_profile("edge_balanced", "linux-arm64-embedded"),
      load_profile("edge_minimal", "linux-arm64-embedded"),
      load_profile("cloud_full", "linux-x86_64-server"),
      load_profile("factory_test", "linux-arm64-factory"),
  };
  const dasall::profiles::ProfileCompatibilityValidator validator;

  for (const auto& profile : profiles) {
    const auto report = validator.validate(
        *profile.snapshot,
        profile.manifest,
        dasall::profiles::ProfileRuntimeEnvironment{
            .target_platform = profile.target_platform,
            .available_modules = profile.manifest.enabled_modules,
            .available_adapters = profile.manifest.enabled_adapters,
        });
    assert_true(report.can_activate(),
                "memory profile integration should remain compatible with the shared profile validator for " +
                    profile.profile_id);
    assert_true(report.compatibility_state ==
                    dasall::profiles::ProfileCompatibilityState::Compatible,
                "memory profile integration should keep the validator in the compatible state for " +
                    profile.profile_id);
  }
}

void test_memory_profile_compatibility_test_is_registered_for_discoverability() {
  const std::filesystem::path integration_cmake_path =
      std::filesystem::path(DASALL_MEMORY_INTEGRATION_TEST_DIR) / "CMakeLists.txt";
  const std::string integration_cmake = read_text_file(integration_cmake_path);

  assert_true(integration_cmake.find("MemoryProfileCompatibilityTest") != std::string::npos,
              "memory integration CMake should register MemoryProfileCompatibilityTest for discoverability");
  assert_true(integration_cmake.find(
                  "dasall_memory_profile_compatibility_integration_test") !=
                  std::string::npos,
              "memory integration CMake should expose a namespaced profile compatibility target");
}

}  // namespace

int main() {
  try {
    test_memory_profile_projection_tracks_vector_maintenance_and_budget_differences();
    test_memory_profile_projection_stays_compatible_with_profile_validator();
    test_memory_profile_compatibility_test_is_registered_for_discoverability();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}