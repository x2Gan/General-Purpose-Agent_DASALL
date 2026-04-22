#include <exception>
#include <iostream>
#include <memory>

#include "facade/KnowledgeService.h"
#include "health/KnowledgeHealthProbe.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::FreshnessSnapshot;
using dasall::knowledge::FreshnessState;
using dasall::knowledge::HealthProbeDeps;
using dasall::knowledge::HealthState;
using dasall::knowledge::IndexManifest;
using dasall::knowledge::KnowledgeTelemetryStatus;
using dasall::knowledge::facade::KnowledgeServiceDeps;
using dasall::knowledge::facade::KnowledgeServiceFacade;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] IndexManifest make_manifest() {
  IndexManifest manifest;
  manifest.tokenizer_profile = "porter unicode61 remove_diacritics 1";
  manifest.snapshot_id = "snapshot-health-001";
  manifest.built_at = 1713657599000;
  manifest.effective_at = 1713657600000;
  manifest.document_count = 1U;
  manifest.chunk_count = 1U;
  manifest.vector_enabled = true;
  return manifest;
}

void test_facade_collects_full_health_snapshot_via_real_health_probe_owner() {
  HealthProbeDeps probe_deps;
  probe_deps.knowledge_enabled = [] { return true; };
  probe_deps.lifecycle_ready = [] { return true; };
  probe_deps.active_manifest = [] {
    return std::optional<IndexManifest>(make_manifest());
  };
  probe_deps.freshness_snapshot = [] {
    FreshnessSnapshot snapshot;
    snapshot.state = FreshnessState::Fresh;
    snapshot.age_ms = 10;
    return snapshot;
  };
  probe_deps.vector_backend_available = [] { return true; };
  probe_deps.last_known_good_available = [] { return true; };
  probe_deps.telemetry_status = [] { return KnowledgeTelemetryStatus{}; };
  probe_deps.degraded_return_count = [] { return static_cast<std::uint64_t>(0U); };
  probe_deps.recent_reason_codes = [] { return std::vector<std::string>{}; };

  KnowledgeServiceDeps deps;
  deps.health_probe = std::make_unique<dasall::knowledge::KnowledgeHealthProbe>(
      std::move(probe_deps));

  KnowledgeServiceFacade facade(std::move(deps));
  const auto snapshot = facade.health_snapshot();

  assert_true(snapshot.has_consistent_values(),
              "health facade should preserve the public health snapshot shape");
  assert_equal(static_cast<int>(HealthState::Healthy), static_cast<int>(snapshot.state),
               "real health probe owner should drive a healthy facade snapshot");
  assert_equal("snapshot-health-001", snapshot.active_snapshot_id,
               "health snapshot should expose the active manifest snapshot id");
  assert_true(snapshot.reason_codes.empty(),
              "healthy health snapshot should not accumulate degradation reason codes");
  assert_true(snapshot.vector_backend_available,
              "healthy health snapshot should report vector backend availability");
}

}  // namespace

int main() {
  try {
    test_facade_collects_full_health_snapshot_via_real_health_probe_owner();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}