#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "health/KnowledgeHealthProbe.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::FreshnessSnapshot;
using dasall::knowledge::FreshnessState;
using dasall::knowledge::HealthProbeDeps;
using dasall::knowledge::HealthState;
using dasall::knowledge::IndexManifest;
using dasall::knowledge::KnowledgeHealthProbe;
using dasall::knowledge::KnowledgeTelemetryStatus;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] IndexManifest make_manifest(bool vector_enabled = true) {
  return IndexManifest{
      .format_version = 1U,
      .lexical_backend = "sqlite_fts5",
      .tokenizer_profile = "unicode61",
      .snapshot_id = "snapshot-health-01",
      .built_at = 1712743200000LL,
      .effective_at = 1712743260000LL,
      .document_count = 8U,
      .chunk_count = 24U,
      .vector_enabled = vector_enabled,
  };
}

[[nodiscard]] FreshnessSnapshot make_fresh_snapshot() {
  return FreshnessSnapshot{
      .state = FreshnessState::Fresh,
      .age_ms = 2000,
      .stale_read_allowed = false,
      .rebuild_recommended = false,
      .reason_codes = {},
  };
}

[[nodiscard]] HealthProbeDeps make_deps() {
  return HealthProbeDeps{
      .knowledge_enabled = [] { return true; },
      .lifecycle_ready = [] { return true; },
      .active_manifest = [] { return std::optional<IndexManifest>{make_manifest()}; },
      .freshness_snapshot = [] { return make_fresh_snapshot(); },
      .vector_backend_available = [] { return true; },
      .last_known_good_available = [] { return true; },
      .telemetry_status = [] { return KnowledgeTelemetryStatus{}; },
      .degraded_return_count = [] { return 0U; },
      .recent_reason_codes = [] { return std::vector<std::string>{}; },
  };
}

void test_knowledge_health_probe_collects_healthy_snapshot() {
  KnowledgeHealthProbe probe(make_deps());
  const auto snapshot = probe.collect();

  assert_true(snapshot.has_consistent_values(),
              "healthy knowledge health snapshot should satisfy the public shape");
  assert_true(snapshot.state == HealthState::Healthy,
              "ready knowledge dependencies should collect a healthy snapshot");
  assert_equal(std::string("snapshot-health-01"),
               snapshot.active_snapshot_id,
               "healthy health snapshot should expose the active snapshot id");
  assert_true(snapshot.freshness_state == FreshnessState::Fresh &&
                  snapshot.vector_backend_available &&
                  snapshot.last_known_good_available &&
                  snapshot.degraded_return_count == 0U &&
                  snapshot.reason_codes.empty(),
              "healthy knowledge health snapshot should keep freshness/vector/lkg facts aligned");
}

void test_knowledge_health_probe_marks_missing_active_snapshot_as_unhealthy() {
  auto deps = make_deps();
  deps.active_manifest = [] { return std::optional<IndexManifest>{}; };
  deps.freshness_snapshot = [] {
    return FreshnessSnapshot{
        .state = FreshnessState::StaleRejected,
        .age_ms = 90000,
        .stale_read_allowed = false,
        .rebuild_recommended = true,
        .reason_codes = {"catalog_expired"},
    };
  };
  deps.last_known_good_available = [] { return false; };
  deps.vector_backend_available = [] { return false; };

  KnowledgeHealthProbe probe(std::move(deps));
  const auto snapshot = probe.collect();

  assert_true(snapshot.has_consistent_values(),
              "unhealthy knowledge health snapshot should still satisfy the public shape");
  assert_true(snapshot.state == HealthState::Unhealthy,
              "missing active snapshot without a last-known-good should be unhealthy");
  assert_true(snapshot.active_snapshot_id.empty() && !snapshot.last_known_good_available,
              "unhealthy knowledge health snapshot should report active snapshot loss");
}

}  // namespace

int main() {
  try {
    test_knowledge_health_probe_collects_healthy_snapshot();
    test_knowledge_health_probe_marks_missing_active_snapshot_as_unhealthy();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}