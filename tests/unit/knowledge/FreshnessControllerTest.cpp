#include <algorithm>
#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include "health/FreshnessController.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::FreshnessController;
using dasall::knowledge::FreshnessState;
using dasall::knowledge::IndexManifest;
using dasall::knowledge::KnowledgeConfigSnapshot;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] KnowledgeConfigSnapshot make_config_snapshot(bool allow_stale_read = false) {
  KnowledgeConfigSnapshot config;
  config.knowledge_enabled = true;
  config.vector_enabled = false;
  config.retrieval_mode_default = dasall::knowledge::RetrievalMode::LexicalOnly;
  config.evidence_budget_tokens = 1024U;
  config.max_context_projection_items = 6U;
  config.catalog_refresh_interval_ms = 30000;
  config.catalog_expire_after_ms = 120000;
  config.allow_stale_read = allow_stale_read;
  config.failure_backoff_ms = 5000;
  config.request_deadline_ms = 1500;
  config.allow_budget_degrade = true;
  config.max_parallel_recall = 1U;
  config.sparse_recall_timeout_ms = 525;
  config.dense_recall_timeout_ms = 525;
  config.ingest_timeout_ms = 10000;
  return config;
}

[[nodiscard]] IndexManifest make_manifest(std::int64_t built_at = 1000,
                                          std::int64_t effective_at = 2000) {
  IndexManifest manifest;
  manifest.format_version = 1U;
  manifest.lexical_backend = "sqlite_fts5";
  manifest.tokenizer_profile = "porter unicode61 remove_diacritics 1";
  manifest.snapshot_id = "snapshot-knowledge-v1";
  manifest.built_at = built_at;
  manifest.effective_at = effective_at;
  manifest.document_count = 120U;
  manifest.chunk_count = 480U;
  manifest.vector_enabled = false;
  return manifest;
}

void assert_has_reason_code(const std::vector<std::string>& reason_codes,
                            const std::string& reason_code,
                            const std::string& message) {
  assert_true(std::find(reason_codes.begin(), reason_codes.end(), reason_code) !=
                  reason_codes.end(),
              message);
}

void test_freshness_controller_returns_unknown_when_manifest_is_missing() {
  const FreshnessController controller;
  const auto snapshot = controller.evaluate(std::nullopt, make_config_snapshot(), 50000, false);

  assert_true(snapshot.has_consistent_values(),
              "missing manifest should still produce an internally consistent freshness snapshot");
  assert_true(snapshot.state == FreshnessState::Unknown,
              "missing manifest must not be treated as fresh");
  assert_equal(0, static_cast<int>(snapshot.age_ms),
               "missing manifest should not fabricate a freshness age");
  assert_true(snapshot.rebuild_recommended,
              "missing manifest should recommend rebuild or refresh");
  assert_has_reason_code(snapshot.reason_codes, "manifest_missing",
                         "missing manifest should leave a manifest_missing reason code");
}

void test_freshness_controller_treats_invalid_manifest_timestamps_as_unknown() {
  const FreshnessController controller;
  const auto snapshot =
      controller.evaluate(make_manifest(3000, 2000), make_config_snapshot(), 50000, false);

  assert_true(snapshot.has_consistent_values(),
              "invalid manifest timestamps should still produce an internally consistent freshness snapshot");
  assert_true(snapshot.state == FreshnessState::Unknown,
              "invalid timestamps must not be treated as fresh or stale-allowed");
  assert_true(snapshot.rebuild_recommended,
              "invalid manifest timestamps should recommend rebuild or refresh");
  assert_has_reason_code(snapshot.reason_codes, "manifest_timestamp_invalid",
                         "invalid manifest timestamps should surface an explicit reason code");
}

void test_freshness_controller_marks_recent_snapshot_as_fresh_and_deterministic() {
  const FreshnessController controller;
  const auto config = make_config_snapshot();
  const auto manifest = make_manifest(1000, 2000);
  const auto first_snapshot = controller.evaluate(manifest, config, 12000, false);
  const auto second_snapshot = controller.evaluate(manifest, config, 12000, false);

  assert_true(first_snapshot.has_consistent_values(),
              "fresh snapshot should satisfy all freshness invariants");
  assert_true(first_snapshot.state == FreshnessState::Fresh,
              "snapshot younger than refresh interval should remain fresh");
  assert_equal(10000, static_cast<int>(first_snapshot.age_ms),
               "freshness age should derive from now_ms minus manifest effective_at");
  assert_true(!first_snapshot.rebuild_recommended,
              "fresh snapshots should not recommend rebuild");
  assert_true(!first_snapshot.stale_read_allowed,
              "fresh snapshots should not be marked as stale reads");
  assert_has_reason_code(first_snapshot.reason_codes, "within_refresh_interval",
                         "fresh snapshot should explain why it remained fresh");

  assert_true(second_snapshot.state == first_snapshot.state,
              "freshness evaluation should be deterministic across repeated invocations");
  assert_equal(static_cast<int>(first_snapshot.age_ms), static_cast<int>(second_snapshot.age_ms),
               "freshness evaluation should recompute the same age for identical inputs");
  assert_true(second_snapshot.reason_codes == first_snapshot.reason_codes,
              "freshness evaluation should not accumulate hidden mutable state across calls");
}

}  // namespace

int main() {
  try {
    test_freshness_controller_returns_unknown_when_manifest_is_missing();
    test_freshness_controller_treats_invalid_manifest_timestamps_as_unknown();
    test_freshness_controller_marks_recent_snapshot_as_fresh_and_deterministic();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}