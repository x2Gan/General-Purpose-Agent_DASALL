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
using dasall::knowledge::KnowledgeQueryKind;
using dasall::knowledge::RetrievalMode;
using dasall::knowledge::TrustLevel;
using dasall::knowledge::index::CorpusCatalog;
using dasall::knowledge::query::CorpusRouter;
using dasall::knowledge::query::NormalizedQuery;
using dasall::tests::support::assert_true;

[[nodiscard]] KnowledgeConfigSnapshot make_config_snapshot() {
  KnowledgeConfigSnapshot config;
  config.knowledge_enabled = true;
  config.vector_enabled = true;
  config.retrieval_mode_default = RetrievalMode::Hybrid;
  config.evidence_budget_tokens = 1024U;
  config.max_context_projection_items = 8U;
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

[[nodiscard]] CorpusDescriptor make_descriptor(std::string corpus_id,
                                               std::vector<RetrievalMode> supported_modes) {
  CorpusDescriptor descriptor;
  descriptor.corpus_id = std::move(corpus_id);
  descriptor.display_name = descriptor.corpus_id;
  descriptor.source_uri = "docs/" + descriptor.corpus_id;
  descriptor.trust_level = TrustLevel::Trusted;
  descriptor.authority_level = AuthorityLevel::Advisory;
  descriptor.include_globs = {"*.md"};
  descriptor.supported_modes = std::move(supported_modes);
  descriptor.active_snapshot_id = descriptor.corpus_id + "-snapshot";
  descriptor.last_updated_ms = 1234;
  descriptor.tags = {"runtime"};
  descriptor.metadata = {
      {"baseline_class", "knowledge"},
      {"owner_module", "knowledge"},
      {"refresh_strategy", "manual"},
      {"default_language", "en"},
  };
  return descriptor;
}

[[nodiscard]] CorpusCatalog make_catalog(std::vector<CorpusDescriptor> descriptors) {
  CorpusCatalog catalog;
  const bool replaced = catalog.replace_all(std::move(descriptors));
  assert_true(replaced, "catalog fixture should accept consistent descriptors");
  return catalog;
}

[[nodiscard]] NormalizedQuery make_query() {
  NormalizedQuery query;
  query.request_id = "req-corpus-router-mode";
  query.normalized_text = "runtime degradation signals";
  query.lexical_terms = {"runtime", "degradation", "signals"};
  query.preferred_mode = {};
  query.domain_tags = {"runtime"};
  query.allowed_corpora = {};
  query.required_tags = {};
  query.required_language = {};
  query.query_kind = KnowledgeQueryKind::DiagnosticContext;
  query.top_k = 12U;
  query.max_context_projection_items = 8U;
  query.prefer_exact_match = false;
  query.allow_stale = false;
  query.warnings = {};
  return query;
}

[[nodiscard]] FreshnessSnapshot make_fresh_snapshot() {
  FreshnessSnapshot snapshot;
  snapshot.state = FreshnessState::Fresh;
  snapshot.age_ms = 1000;
  snapshot.reason_codes = {"within_refresh_interval"};
  return snapshot;
}

void assert_reason_present(const std::vector<std::string>& reason_codes, const std::string& reason_code) {
  assert_true(std::find(reason_codes.begin(), reason_codes.end(), reason_code) != reason_codes.end(),
              "expected route reason missing: " + reason_code);
}

void test_corpus_router_selects_dense_only_for_diagnostic_queries_when_all_corpora_are_dense_capable() {
  CorpusRouter router;
  auto catalog = make_catalog({
      make_descriptor("runtime-dense-a", {RetrievalMode::DenseOnly}),
      make_descriptor("runtime-dense-b", {RetrievalMode::DenseOnly, RetrievalMode::Hybrid}),
  });

  const auto result = router.build_plan(make_query(),
                                        make_config_snapshot(),
                                        catalog.snapshot(),
                                        make_fresh_snapshot());

  assert_true(result.has_consistent_values(),
              "dense-only routing result should stay internally consistent");
  assert_true(result.ok, "diagnostic query should route successfully when all corpora are dense-capable");
  assert_true(result.plan->mode == RetrievalMode::DenseOnly,
              "diagnostic query should prefer dense-only when vector path is fully available");
  assert_reason_present(result.route_reason_codes, "mode_dense_only");
}

void test_corpus_router_degrades_to_lexical_when_no_candidate_supports_dense() {
  CorpusRouter router;
  auto catalog = make_catalog({
      make_descriptor("runtime-lexical", {RetrievalMode::LexicalOnly}),
      make_descriptor("runtime-reference", {RetrievalMode::LexicalOnly}),
  });

  const auto result = router.build_plan(make_query(),
                                        make_config_snapshot(),
                                        catalog.snapshot(),
                                        make_fresh_snapshot());

  assert_true(result.has_consistent_values(),
              "lexical fallback route should stay internally consistent");
  assert_true(result.ok, "router should degrade to lexical instead of failing when dense support is incomplete");
  assert_true(result.plan->mode == RetrievalMode::LexicalOnly,
              "diagnostic query should degrade to lexical-only when no dense-capable corpus exists");
  assert_reason_present(result.route_reason_codes, "mode_lexical_only");
  assert_reason_present(result.route_reason_codes, "corpus_mode_capability_downgraded");
}

}  // namespace

int main() {
  try {
    test_corpus_router_selects_dense_only_for_diagnostic_queries_when_all_corpora_are_dense_capable();
    test_corpus_router_degrades_to_lexical_when_no_candidate_supports_dense();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}