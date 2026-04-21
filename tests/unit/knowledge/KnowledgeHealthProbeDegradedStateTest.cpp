#include <algorithm>
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
using dasall::tests::support::assert_true;

[[nodiscard]] bool has_reason_code(
    const dasall::knowledge::KnowledgeHealthSnapshot& snapshot,
    const std::string& reason_code) {
  return std::find(snapshot.reason_codes.begin(),
                   snapshot.reason_codes.end(),
                   reason_code) != snapshot.reason_codes.end();
}

void test_knowledge_health_probe_keeps_lexical_only_path_degraded() {
  HealthProbeDeps deps{
      .knowledge_enabled = [] { return true; },
      .lifecycle_ready = [] { return true; },
      .active_manifest = [] {
        return std::optional<IndexManifest>{IndexManifest{
            .format_version = 1U,
            .lexical_backend = "sqlite_fts5",
            .tokenizer_profile = "unicode61",
            .snapshot_id = "snapshot-health-degraded",
            .built_at = 1712743200000LL,
            .effective_at = 1712743260000LL,
            .document_count = 12U,
            .chunk_count = 32U,
            .vector_enabled = true,
        }};
      },
      .freshness_snapshot = [] {
        return FreshnessSnapshot{
            .state = FreshnessState::Fresh,
            .age_ms = 4000,
            .stale_read_allowed = false,
            .rebuild_recommended = false,
            .reason_codes = {},
        };
      },
      .vector_backend_available = [] { return false; },
      .last_known_good_available = [] { return true; },
      .telemetry_status = [] { return KnowledgeTelemetryStatus{}; },
      .degraded_return_count = [] { return 2U; },
      .recent_reason_codes = [] {
        return std::vector<std::string>{"dense_lane_timeout"};
      },
  };

  KnowledgeHealthProbe probe(std::move(deps));
  const auto snapshot = probe.collect();

  assert_true(snapshot.has_consistent_values(),
              "degraded knowledge health snapshot should satisfy the public shape");
  assert_true(snapshot.state == HealthState::Degraded,
              "vector backend loss with an active lexical snapshot should stay degraded");
  assert_true(!snapshot.active_snapshot_id.empty() && !snapshot.vector_backend_available &&
                  snapshot.last_known_good_available &&
                  snapshot.degraded_return_count == 2U,
              "degraded lexical-only knowledge state should preserve active snapshot and degraded counters");
  assert_true(has_reason_code(snapshot, "vector_backend_unavailable") &&
                  has_reason_code(snapshot, "dense_lane_timeout"),
              "degraded lexical-only knowledge state should expose vector and route reason codes");
}

}  // namespace

int main() {
  try {
    test_knowledge_health_probe_keeps_lexical_only_path_degraded();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}