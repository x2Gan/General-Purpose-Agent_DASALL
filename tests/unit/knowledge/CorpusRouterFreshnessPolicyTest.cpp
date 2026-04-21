#include <algorithm>
#include <exception>
#include <iostream>
#include <string>

#include "health/FreshnessController.h"
#include "index/CorpusCatalog.h"
#include "query/CorpusRouter.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::CorpusDescriptor;
using dasall::knowledge::FreshnessSnapshot;
using dasall::knowledge::FreshnessState;
using dasall::knowledge::KnowledgeConfigSnapshot;
using dasall::knowledge::KnowledgeErrorCode;
using dasall::knowledge::KnowledgeQueryKind;
using dasall::knowledge::RetrievalMode;
using dasall::knowledge::TrustLevel;
using dasall::knowledge::index::CorpusCatalog;
using dasall::knowledge::query::CorpusRouter;
using dasall::knowledge::query::NormalizedQuery;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] KnowledgeConfigSnapshot make_config_snapshot() {
  KnowledgeConfigSnapshot config;
  config.knowledge_enabled = true;
  config.vector_enabled = true;
  config.retrieval_mode_default = RetrievalMode::Hybrid;
  config.evidence_budget_tokens = 1024U;
  config.max_context_projection_items = 6U;
  config.catalog_refresh_interval_ms = 30000;
  config.catalog_expire_after_ms = 120000;
  config.allow_stale_read = true;
  config.failure_backoff_ms = 5000;
  config.request_deadline_ms = 1500;
  config.allow_budget_degrade = true;
  config.max_parallel_recall = 2U;
  config.sparse_recall_timeout_ms = 525;
  config.dense_recall_timeout_ms = 525;
  config.ingest_timeout_ms = 10000;
  return config;
}

[[nodiscard]] CorpusCatalog make_catalog() {
  CorpusDescriptor descriptor;
  descriptor.corpus_id = "architecture-reference";
  descriptor.display_name = descriptor.corpus_id;
  descriptor.source_uri = "docs/architecture";
  descriptor.trust_level = TrustLevel::Trusted;
  descriptor.authority_level = AuthorityLevel::Reference;
  descriptor.include_globs = {"*.md"};
  descriptor.supported_modes = {RetrievalMode::LexicalOnly, RetrievalMode::Hybrid};
  descriptor.active_snapshot_id = "snapshot-architecture";
  descriptor.last_updated_ms = 1234;
  descriptor.tags = {"architecture"};
  descriptor.metadata = {
      {"baseline_class", "knowledge"},
      {"owner_module", "knowledge"},
      {"refresh_strategy", "manual"},
      {"default_language", "en"},
  };

  CorpusCatalog catalog;
  const bool replaced = catalog.replace_all({descriptor});
  assert_true(replaced, "catalog fixture should accept consistent descriptors");
  return catalog;
}

[[nodiscard]] NormalizedQuery make_query(bool allow_stale) {
  return NormalizedQuery{
      .request_id = "req-corpus-router-freshness",
      .normalized_text = "stale snapshot policy",
      .lexical_terms = {"stale", "snapshot", "policy"},
      .domain_tags = {"architecture"},
      .allowed_corpora = {},
      .query_kind = KnowledgeQueryKind::FactLookup,
      .top_k = 8U,
      .max_context_projection_items = 6U,
      .prefer_exact_match = true,
      .allow_stale = allow_stale,
      .warnings = {},
  };
}

[[nodiscard]] FreshnessSnapshot make_snapshot(FreshnessState state, bool stale_read_allowed) {
  FreshnessSnapshot snapshot;
  snapshot.state = state;
  snapshot.age_ms = state == FreshnessState::Fresh ? 1000 : 50000;
  snapshot.stale_read_allowed = stale_read_allowed;
  snapshot.rebuild_recommended = state != FreshnessState::Fresh;
  snapshot.reason_codes = {state == FreshnessState::Fresh ? "within_refresh_interval"
                                                          : "refresh_interval_elapsed"};
  return snapshot;
}

void assert_reason_present(const std::vector<std::string>& reason_codes, const std::string& reason_code) {
  assert_true(std::find(reason_codes.begin(), reason_codes.end(), reason_code) != reason_codes.end(),
              "expected route reason missing: " + reason_code);
}

void test_corpus_router_allows_stale_snapshot_only_when_freshness_and_query_agree() {
  CorpusRouter router;
  auto catalog = make_catalog();

  const auto result = router.build_plan(make_query(true),
                                        make_config_snapshot(),
                                        catalog.snapshot(),
                                        make_snapshot(FreshnessState::StaleAllowed, true));

  assert_true(result.has_consistent_values(),
              "stale-allowed routing result should remain internally consistent");
  assert_true(result.ok, "stale-allowed freshness should still produce a route plan");
  assert_true(result.plan->allow_stale_snapshot,
              "route plan should mark allow_stale_snapshot when freshness explicitly allows it");
  assert_reason_present(result.route_reason_codes, "stale_snapshot_allowed");
}

void test_corpus_router_rejects_stale_snapshot_when_policy_forbids_it() {
  CorpusRouter router;
  auto catalog = make_catalog();

  const auto result = router.build_plan(make_query(false),
                                        make_config_snapshot(),
                                        catalog.snapshot(),
                                        make_snapshot(FreshnessState::StaleRejected, false));

  assert_true(result.has_consistent_values(),
              "stale-rejected routing result should still be structured");
  assert_true(!result.ok, "stale-rejected freshness must block routing");
  assert_equal(static_cast<int>(KnowledgeErrorCode::IndexStaleRejected),
               *result.error->details.code,
               "stale-rejected freshness should map to IndexStaleRejected");
  assert_reason_present(result.route_reason_codes, "stale_snapshot_rejected");
}

}  // namespace

int main() {
  try {
    test_corpus_router_allows_stale_snapshot_only_when_freshness_and_query_agree();
    test_corpus_router_rejects_stale_snapshot_when_policy_forbids_it();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}