#include <algorithm>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "health/KnowledgeHealthProbe.h"
#include "support/TestAssertions.h"

namespace {

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

void test_knowledge_health_probe_marks_missing_dependencies_unknown() {
  HealthProbeDeps deps{
      .knowledge_enabled = [] { return true; },
      .lifecycle_ready = [] { return true; },
      .active_manifest = [] {
        return std::optional<IndexManifest>{IndexManifest{
            .format_version = 1U,
            .lexical_backend = "sqlite_fts5",
            .tokenizer_profile = "unicode61",
            .snapshot_id = "snapshot-health-unknown",
            .built_at = 1712743200000LL,
            .effective_at = 1712743260000LL,
            .document_count = 4U,
            .chunk_count = 11U,
            .vector_enabled = true,
        }};
      },
      .freshness_snapshot = {},
      .vector_backend_available = [] { return true; },
      .last_known_good_available = [] { return true; },
      .telemetry_status = [] { return KnowledgeTelemetryStatus{}; },
      .degraded_return_count = [] { return 0U; },
      .recent_reason_codes = [] { return std::vector<std::string>{}; },
  };

  KnowledgeHealthProbe probe(std::move(deps));
  const auto snapshot = probe.collect();

  assert_true(snapshot.has_consistent_values(),
              "unknown knowledge health snapshot should still satisfy the public shape");
  assert_true(snapshot.state == HealthState::Unknown,
              "missing freshness dependency should not be misclassified as healthy");
  assert_true(snapshot.freshness_state == FreshnessState::Unknown &&
                  has_reason_code(snapshot, "freshness_provider_missing"),
              "unknown knowledge health snapshot should expose the missing dependency reason code");
}

}  // namespace

int main() {
  try {
    test_knowledge_health_probe_marks_missing_dependencies_unknown();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}