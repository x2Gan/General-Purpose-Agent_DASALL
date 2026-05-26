#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

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

[[nodiscard]] KnowledgeConfigSnapshot make_config_snapshot(RetrievalMode default_mode) {
  KnowledgeConfigSnapshot config;
  config.knowledge_enabled = true;
  config.vector_enabled = true;
  config.retrieval_mode_default = default_mode;
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
  descriptor.authority_level = AuthorityLevel::Reference;
  descriptor.include_globs = {"*.md"};
  descriptor.supported_modes = std::move(supported_modes);
  descriptor.active_snapshot_id = descriptor.corpus_id + "-snapshot";
  descriptor.last_updated_ms = 1234;
  descriptor.tags = {"mixed"};
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

[[nodiscard]] FreshnessSnapshot make_fresh_snapshot() {
  FreshnessSnapshot snapshot;
  snapshot.state = FreshnessState::Fresh;
  snapshot.age_ms = 1000;
  snapshot.reason_codes = {"within_refresh_interval"};
  return snapshot;
}

[[nodiscard]] NormalizedQuery make_hybrid_query() {
  NormalizedQuery query;
  query.request_id = "req-corpus-router-mixed-hybrid";
  query.normalized_text = "runtime owner boundary hybrid";
  query.lexical_terms = {"runtime", "owner", "boundary", "hybrid"};
  query.preferred_mode = {};
  query.domain_tags = {"mixed"};
  query.allowed_corpora = {};
  query.required_tags = {};
  query.required_language = {};
  query.query_kind = KnowledgeQueryKind::FactLookup;
  query.top_k = 8U;
  query.max_context_projection_items = 8U;
  query.prefer_exact_match = false;
  query.allow_stale = false;
  query.warnings = {};
  return query;
}

[[nodiscard]] NormalizedQuery make_dense_only_query() {
  NormalizedQuery query;
  query.request_id = "req-corpus-router-mixed-dense";
  query.normalized_text = "runtime owner boundary diagnostic";
  query.lexical_terms = {"runtime", "owner", "boundary", "diagnostic"};
  query.preferred_mode = {};
  query.domain_tags = {"mixed"};
  query.allowed_corpora = {};
  query.required_tags = {};
  query.required_language = {};
  query.query_kind = KnowledgeQueryKind::DiagnosticContext;
  query.top_k = 8U;
  query.max_context_projection_items = 8U;
  query.prefer_exact_match = false;
  query.allow_stale = false;
  query.warnings = {};
  return query;
}

void assert_reason_present(const std::vector<std::string>& reason_codes,
                           const std::string& reason_code) {
  assert_true(std::find(reason_codes.begin(), reason_codes.end(), reason_code) != reason_codes.end(),
              "expected route reason missing: " + reason_code);
}

void test_corpus_router_narrows_hybrid_route_to_dense_capable_subset() {
  CorpusRouter router;
  auto catalog = make_catalog({
      make_descriptor("architecture_reference",
                      {RetrievalMode::LexicalOnly, RetrievalMode::Hybrid}),
      make_descriptor("profile_policy_normative", {RetrievalMode::LexicalOnly}),
  });

  const auto result = router.build_plan(make_hybrid_query(),
                                        make_config_snapshot(RetrievalMode::Hybrid),
                                        catalog.snapshot(),
                                        make_fresh_snapshot());

  assert_true(result.has_consistent_values(),
              "mixed-capability hybrid route should stay internally consistent");
  assert_true(result.ok,
              "router should not degrade a mixed-capability query when a dense-capable subset exists");
  assert_true(result.plan->mode == RetrievalMode::Hybrid,
              "mixed-capability fact lookup should keep hybrid when a dense-capable subset exists");
  assert_true(result.plan->corpus_ids == std::vector<std::string>({"architecture_reference"}),
              "mixed-capability hybrid route should narrow to the dense-capable subset");
  assert_reason_present(result.route_reason_codes, "mode_hybrid");
  assert_reason_present(result.route_reason_codes, "dense_capable_subset_selected");
}

void test_corpus_router_narrows_dense_only_route_to_dense_capable_subset() {
  CorpusRouter router;
  auto catalog = make_catalog({
      make_descriptor("architecture_reference",
                      {RetrievalMode::LexicalOnly, RetrievalMode::Hybrid}),
      make_descriptor("profile_policy_normative", {RetrievalMode::LexicalOnly}),
  });

  const auto result = router.build_plan(make_dense_only_query(),
                                        make_config_snapshot(RetrievalMode::DenseOnly),
                                        catalog.snapshot(),
                                        make_fresh_snapshot());

  assert_true(result.has_consistent_values(),
              "mixed-capability dense-only route should stay internally consistent");
  assert_true(result.ok,
              "router should not degrade a mixed-capability diagnostic query when a dense-capable subset exists");
  assert_true(result.plan->mode == RetrievalMode::DenseOnly,
              "mixed-capability diagnostic query should keep dense-only when a dense-capable subset exists");
  assert_true(result.plan->corpus_ids == std::vector<std::string>({"architecture_reference"}),
              "mixed-capability dense-only route should narrow to the dense-capable subset");
  assert_reason_present(result.route_reason_codes, "mode_dense_only");
  assert_reason_present(result.route_reason_codes, "dense_capable_subset_selected");
}

}  // namespace

int main() {
  try {
    test_corpus_router_narrows_hybrid_route_to_dense_capable_subset();
    test_corpus_router_narrows_dense_only_route_to_dense_capable_subset();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}