#include <algorithm>
#include <exception>
#include <iostream>
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

[[nodiscard]] KnowledgeConfigSnapshot make_config_snapshot(bool allow_stale_read) {
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

[[nodiscard]] IndexManifest make_manifest() {
  IndexManifest manifest;
  manifest.format_version = 1U;
  manifest.lexical_backend = "sqlite_fts5";
  manifest.tokenizer_profile = "porter unicode61 remove_diacritics 1";
  manifest.snapshot_id = "snapshot-knowledge-v1";
  manifest.built_at = 1000;
  manifest.effective_at = 2000;
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

void test_freshness_controller_allows_stale_only_inside_the_expiry_window() {
  const FreshnessController controller;
  const auto snapshot = controller.evaluate(make_manifest(), make_config_snapshot(true), 62000, true);

  assert_true(snapshot.has_consistent_values(),
              "stale-allowed snapshot should satisfy all freshness invariants");
  assert_true(snapshot.state == FreshnessState::StaleAllowed,
              "snapshot older than refresh interval but younger than expiry should allow stale reads when both gates opt in");
  assert_equal(60000, static_cast<int>(snapshot.age_ms),
               "stale age should derive from manifest effective_at");
  assert_true(snapshot.stale_read_allowed,
              "stale-allowed path should explicitly mark stale_read_allowed");
  assert_true(snapshot.rebuild_recommended,
              "stale-allowed path should still recommend rebuild or refresh");
  assert_has_reason_code(snapshot.reason_codes, "refresh_interval_elapsed",
                         "stale-allowed path should record the refresh interval overrun");
  assert_has_reason_code(snapshot.reason_codes, "stale_read_allowed",
                         "stale-allowed path should record the allow decision");
}

void test_freshness_controller_rejects_stale_when_query_opt_in_is_missing() {
  const FreshnessController controller;
  const auto snapshot = controller.evaluate(make_manifest(), make_config_snapshot(true), 62000, false);

  assert_true(snapshot.has_consistent_values(),
              "stale-rejected snapshot should remain internally consistent");
  assert_true(snapshot.state == FreshnessState::StaleRejected,
              "stale path should be rejected when query does not opt in");
  assert_true(!snapshot.stale_read_allowed,
              "stale-rejected path must not expose stale_read_allowed");
  assert_true(snapshot.rebuild_recommended,
              "stale rejection should still recommend rebuild or refresh");
  assert_has_reason_code(snapshot.reason_codes, "refresh_interval_elapsed",
                         "stale rejection should preserve the freshness overrun reason");
  assert_has_reason_code(snapshot.reason_codes, "query_stale_opt_in_missing",
                         "stale rejection should record the missing query opt-in");
}

void test_freshness_controller_rejects_expired_snapshot_even_if_stale_is_allowed() {
  const FreshnessController controller;
  const auto snapshot = controller.evaluate(make_manifest(), make_config_snapshot(true), 130000, true);

  assert_true(snapshot.has_consistent_values(),
              "expired snapshot should still produce a consistent freshness result");
  assert_true(snapshot.state == FreshnessState::StaleRejected,
              "snapshots beyond catalog_expire_after_ms should never be served as stale");
  assert_true(!snapshot.stale_read_allowed,
              "expired snapshots must never remain stale-readable");
  assert_true(snapshot.rebuild_recommended,
              "expired snapshots should recommend rebuild or refresh");
  assert_has_reason_code(snapshot.reason_codes, "catalog_expired",
                         "expired snapshot should record the hard expiry reason");
}

}  // namespace

int main() {
  try {
    test_freshness_controller_allows_stale_only_inside_the_expiry_window();
    test_freshness_controller_rejects_stale_when_query_opt_in_is_missing();
    test_freshness_controller_rejects_expired_snapshot_even_if_stale_is_allowed();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}#include <algorithm>
#include <exception>
#include <iostream>
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

[[nodiscard]] KnowledgeConfigSnapshot make_config_snapshot(bool allow_stale_read) {
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

[[nodiscard]] IndexManifest make_manifest() {
  IndexManifest manifest;
  manifest.format_version = 1U;
  manifest.lexical_backend = "sqlite_fts5";
  manifest.tokenizer_profile = "porter unicode61 remove_diacritics 1";
  manifest.snapshot_id = "snapshot-knowledge-v1";
  manifest.built_at = 1000;
  manifest.effective_at = 2000;
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

void test_freshness_controller_allows_stale_only_inside_the_expiry_window() {
  const FreshnessController controller;
  const auto snapshot = controller.evaluate(make_manifest(), make_config_snapshot(true), 62000, true);

  assert_true(snapshot.has_consistent_values(),
              "stale-allowed snapshot should satisfy all freshness invariants");
  assert_true(snapshot.state == FreshnessState::StaleAllowed,
              "snapshot older than refresh interval but younger than expiry should allow stale reads when both gates opt in");
  assert_equal(60000, static_cast<int>(snapshot.age_ms),
               "stale age should derive from manifest effective_at");
  assert_true(snapshot.stale_read_allowed,
              "stale-allowed path should explicitly mark stale_read_allowed");
  assert_true(snapshot.rebuild_recommended,
              "stale-allowed path should still recommend rebuild or refresh");
  assert_has_reason_code(snapshot.reason_codes, "refresh_interval_elapsed",
                         "stale-allowed path should record the refresh interval overrun");
  assert_has_reason_code(snapshot.reason_codes, "stale_read_allowed",
                         "stale-allowed path should record the allow decision");
}

void test_freshness_controller_rejects_stale_when_query_opt_in_is_missing() {
  const FreshnessController controller;
  const auto snapshot = controller.evaluate(make_manifest(), make_config_snapshot(true), 62000, false);

  assert_true(snapshot.has_consistent_values(),
              "stale-rejected snapshot should remain internally consistent");
  assert_true(snapshot.state == FreshnessState::StaleRejected,
              "stale path should be rejected when query does not opt in");
  assert_true(!snapshot.stale_read_allowed,
              "stale-rejected path must not expose stale_read_allowed");
  assert_true(snapshot.rebuild_recommended,
              "stale rejection should still recommend rebuild or refresh");
  assert_has_reason_code(snapshot.reason_codes, "refresh_interval_elapsed",
                         "stale rejection should preserve the freshness overrun reason");
  assert_has_reason_code(snapshot.reason_codes, "query_stale_opt_in_missing",
                         "stale rejection should record the missing query opt-in");
}

void test_freshness_controller_rejects_expired_snapshot_even_if_stale_is_allowed() {
  const FreshnessController controller;
  const auto snapshot = controller.evaluate(make_manifest(), make_config_snapshot(true), 130000, true);

  assert_true(snapshot.has_consistent_values(),
              "expired snapshot should still produce a consistent freshness result");
  assert_true(snapshot.state == FreshnessState::StaleRejected,
              "snapshots beyond catalog_expire_after_ms should never be served as stale");
  assert_true(!snapshot.stale_read_allowed,
              "expired snapshots must never remain stale-readable");
  assert_true(snapshot.rebuild_recommended,
              "expired snapshots should recommend rebuild or refresh");
  assert_has_reason_code(snapshot.reason_codes, "catalog_expired",
                         "expired snapshot should record the hard expiry reason");
}

}  // namespace

int main() {
  try {
    test_freshness_controller_allows_stale_only_inside_the_expiry_window();
    test_freshness_controller_rejects_stale_when_query_opt_in_is_missing();
    test_freshness_controller_rejects_expired_snapshot_even_if_stale_is_allowed();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}