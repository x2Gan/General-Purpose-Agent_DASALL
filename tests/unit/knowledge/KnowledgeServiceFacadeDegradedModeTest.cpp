#include <exception>
#include <iostream>

#include "facade/KnowledgeService.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::EvidenceBundle;
using dasall::knowledge::EvidenceSlice;
using dasall::knowledge::FreshnessSnapshot;
using dasall::knowledge::FreshnessState;
using dasall::knowledge::HealthState;
using dasall::knowledge::IndexManifest;
using dasall::knowledge::KnowledgeConfigSnapshot;
using dasall::knowledge::KnowledgeQuery;
using dasall::knowledge::KnowledgeQueryKind;
using dasall::knowledge::RetrievalMode;
using dasall::knowledge::facade::KnowledgeServiceDeps;
using dasall::knowledge::facade::KnowledgeServiceFacade;
using dasall::knowledge::query::NormalizeResult;
using dasall::knowledge::query::NormalizedQuery;
using dasall::knowledge::query::RetrievalPlan;
using dasall::knowledge::query::RoutePlanResult;
using dasall::knowledge::rerank::RankedHit;
using dasall::knowledge::rerank::RankedHitSet;
using dasall::knowledge::retrieve::RecallCoordinatorResult;
using dasall::knowledge::retrieve::RecallHit;
using dasall::tests::support::assert_true;

[[nodiscard]] KnowledgeConfigSnapshot make_config() {
  KnowledgeConfigSnapshot config;
  config.knowledge_enabled = true;
  config.vector_enabled = false;
  config.retrieval_mode_default = RetrievalMode::LexicalOnly;
  config.evidence_budget_tokens = 256U;
  config.max_context_projection_items = 4U;
  config.catalog_refresh_interval_ms = 60000;
  config.catalog_expire_after_ms = 120000;
  config.failure_backoff_ms = 1000;
  config.request_deadline_ms = 1000;
  config.max_parallel_recall = 1U;
  config.sparse_recall_timeout_ms = 350;
  config.dense_recall_timeout_ms = 350;
  config.ingest_timeout_ms = 2000;
  return config;
}

[[nodiscard]] KnowledgeQuery make_query() {
  KnowledgeQuery query;
  query.request_id = "req-degraded";
  query.query_text = "policy evidence";
  query.query_kind = KnowledgeQueryKind::PolicyEvidence;
  query.top_k = 4U;
  query.max_context_projection_items = 4U;
  return query;
}

void test_facade_propagates_degraded_recall_to_final_evidence() {
  KnowledgeServiceDeps deps;
  deps.now_ms = [] { return 1000; };
  deps.normalize_query = [](const KnowledgeQuery& query) {
    NormalizeResult result;
    NormalizedQuery normalized_query;
    normalized_query.request_id = query.request_id;
    normalized_query.normalized_text = "policy evidence";
    normalized_query.lexical_terms = {"policy"};
    normalized_query.query_kind = query.query_kind;
    normalized_query.top_k = query.top_k;
    normalized_query.max_context_projection_items = query.max_context_projection_items;
    result.ok = true;
    result.normalized_query = std::move(normalized_query);
    return result;
  };
  deps.current_manifest = [] {
    return std::optional<IndexManifest>(IndexManifest{
        .format_version = 1U,
        .lexical_backend = "sqlite_fts5",
        .tokenizer_profile = "porter unicode61 remove_diacritics 1",
        .snapshot_id = "snapshot-001",
        .built_at = 900,
        .effective_at = 950,
        .document_count = 1U,
        .chunk_count = 1U,
    });
  };
  deps.evaluate_freshness = [](const std::optional<IndexManifest>&,
                               const KnowledgeConfigSnapshot&,
                               std::int64_t,
                               bool) {
    FreshnessSnapshot snapshot;
    snapshot.state = FreshnessState::Fresh;
    snapshot.age_ms = 10;
    return snapshot;
  };
  deps.build_plan = [](const NormalizedQuery&, const KnowledgeConfigSnapshot&, const auto&, const FreshnessSnapshot&) {
    RoutePlanResult result;
    result.ok = true;
    result.plan = RetrievalPlan{
        .mode = RetrievalMode::LexicalOnly,
        .corpus_ids = {"adr_normative"},
        .sparse_top_k = 4U,
        .allow_partial_results = true,
        .max_projection_items = 4U,
        .route_reason_codes = {"route_ok"},
    };
    result.route_reason_codes = {"route_ok"};
    return result;
  };
  deps.recall = [](const auto&) {
    RecallCoordinatorResult result;
    result.ok = true;
    result.candidates.degraded = true;
    result.candidates.sparse_succeeded = true;
    result.candidates.sparse_hits = {RecallHit{
        .corpus_id = "adr_normative",
        .document_id = "adr-001",
        .chunk_id = "chunk-001",
        .score = 0.8F,
        .raw_snippet = "policy evidence snippet",
        .citation_ref = "ADR-001#policy",
        .updated_at = 1713657600000,
        .authority_level = AuthorityLevel::Normative,
        .tags = {"normative"},
    }};
    return result;
  };
  deps.rerank = [](const auto& candidates, const FreshnessSnapshot&, const auto&) {
    RankedHitSet ranked;
    ranked.degraded = true;
    ranked.hits = {RankedHit{
        .hit = candidates.sparse_hits.front(),
        .fused_score = 1.0F,
        .score_reason_codes = {"rrf_sparse_only"},
    }};
    return ranked;
  };
  deps.assemble_evidence = [](const RankedHitSet&, const auto&) {
    EvidenceBundle bundle;
    bundle.slices = {EvidenceSlice{
        .evidence_id = "evidence-001",
        .snippet = "policy evidence snippet",
        .citation_ref = "ADR-001#policy",
        .confidence = 1.0F,
        .freshness = FreshnessState::Fresh,
        .tags = {"normative"},
    }};
    bundle.context_projection = {"[policy] policy evidence snippet"};
    return bundle;
  };
  deps.collect_health_snapshot = [] {
    dasall::knowledge::KnowledgeHealthSnapshot snapshot;
    snapshot.state = HealthState::Healthy;
    snapshot.active_snapshot_id = "snapshot-001";
    snapshot.freshness_state = FreshnessState::Fresh;
    snapshot.vector_backend_available = true;
    snapshot.last_known_good_available = true;
    return snapshot;
  };
  KnowledgeServiceFacade facade(std::move(deps));

  assert_true(facade.init(make_config()),
              "facade should initialize before degraded-mode retrieve");
  const auto result = facade.retrieve(make_query());
  assert_true(result.ok,
              "degraded recall should still return a successful retrieve result");
  assert_true(result.evidence->degraded,
              "facade should propagate degraded recall/rerank state to final evidence");
}

}  // namespace

int main() {
  try {
    test_facade_propagates_degraded_recall_to_final_evidence();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}