#include <algorithm>
#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include "health/FreshnessController.h"
#include "index/CorpusCatalog.h"
#include "query/CorpusRouter.h"
#include "query/QueryNormalizer.h"
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

[[nodiscard]] CorpusDescriptor make_descriptor(std::string corpus_id,
                                               std::vector<std::string> tags,
                                               AuthorityLevel authority_level,
                                               std::vector<RetrievalMode> supported_modes,
                                               TrustLevel trust_level = TrustLevel::Trusted) {
  CorpusDescriptor descriptor;
  descriptor.corpus_id = std::move(corpus_id);
  descriptor.display_name = descriptor.corpus_id;
  descriptor.source_uri = "docs/" + descriptor.corpus_id;
  descriptor.trust_level = trust_level;
  descriptor.authority_level = authority_level;
  descriptor.include_globs = {"*.md"};
  descriptor.supported_modes = std::move(supported_modes);
  descriptor.active_snapshot_id = descriptor.corpus_id + "-snapshot";
  descriptor.last_updated_ms = 1234;
  descriptor.tags = std::move(tags);
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
  return NormalizedQuery{
      .request_id = "req-corpus-router-01",
      .normalized_text = "adr owner boundary",
      .lexical_terms = {"adr", "owner", "boundary"},
      .domain_tags = {"architecture"},
      .allowed_corpora = {},
      .query_kind = KnowledgeQueryKind::FactLookup,
      .top_k = 8U,
      .max_context_projection_items = 6U,
      .prefer_exact_match = true,
      .allow_stale = false,
      .warnings = {},
  };
}

[[nodiscard]] FreshnessSnapshot make_freshness_snapshot(FreshnessState state = FreshnessState::Fresh,
                                                        bool stale_read_allowed = false) {
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

void test_corpus_router_builds_hybrid_plan_for_reference_queries() {
  CorpusRouter router;
  auto catalog = make_catalog({
      make_descriptor("adr-normative", {"architecture", "adr"}, AuthorityLevel::Normative,
                      {RetrievalMode::LexicalOnly, RetrievalMode::Hybrid}),
      make_descriptor("architecture-reference", {"architecture"}, AuthorityLevel::Reference,
                      {RetrievalMode::LexicalOnly, RetrievalMode::Hybrid}),
      make_descriptor("runtime-advisory", {"runtime"}, AuthorityLevel::Advisory,
                      {RetrievalMode::LexicalOnly}),
  });

  const auto result = router.build_plan(make_query(),
                                        make_config_snapshot(),
                                        catalog.snapshot(),
                                        make_freshness_snapshot());

  assert_true(result.has_consistent_values(),
              "hybrid routing result should remain internally consistent");
  assert_true(result.ok, "fresh reference query should produce a retrieval plan");
  assert_true(result.plan->mode == RetrievalMode::Hybrid,
              "fact lookup should choose hybrid when vector is enabled and all candidate corpora are hybrid-capable");
  assert_true(result.plan->corpus_ids ==
                  std::vector<std::string>({"adr-normative", "architecture-reference"}),
              "router should keep only tag-matching corpora that meet the authority floor");
  assert_equal(8, static_cast<int>(result.plan->sparse_top_k),
               "hybrid plan should preserve sparse top_k from the normalized query");
  assert_equal(8, static_cast<int>(result.plan->dense_top_k),
               "hybrid plan should preserve dense top_k from the normalized query");
  assert_true(result.plan->allow_partial_results,
              "hybrid plan should allow partial results when degrade is enabled");
  assert_reason_present(result.route_reason_codes, "domain_tag_filter_applied");
  assert_reason_present(result.route_reason_codes, "authority_filter_applied");
  assert_reason_present(result.route_reason_codes, "mode_hybrid");
}

void test_corpus_router_rejects_empty_candidate_set_instead_of_scanning_everything() {
  CorpusRouter router;
  auto catalog = make_catalog({
      make_descriptor("runtime-reference", {"runtime"}, AuthorityLevel::Reference,
                      {RetrievalMode::LexicalOnly, RetrievalMode::Hybrid}),
  });

  auto query = make_query();
  query.allowed_corpora = {"adr-normative"};

  const auto result = router.build_plan(query,
                                        make_config_snapshot(),
                                        catalog.snapshot(),
                                        make_freshness_snapshot());

  assert_true(result.has_consistent_values(),
              "route-unavailable result should still be structured");
  assert_true(!result.ok, "router must not silently widen to a full-catalog scan when filters exclude all corpora");
  assert_equal(static_cast<int>(dasall::knowledge::KnowledgeErrorCode::NoCorpusAvailable),
               *result.error->details.code,
               "empty candidate set should map to NoCorpusAvailable");
  assert_reason_present(result.route_reason_codes, "no_corpus_available");
}

}  // namespace

int main() {
  try {
    test_corpus_router_builds_hybrid_plan_for_reference_queries();
    test_corpus_router_rejects_empty_candidate_set_instead_of_scanning_everything();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}