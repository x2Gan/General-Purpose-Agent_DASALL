#include <exception>
#include <iostream>
#include <string>
#include <vector>

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
using dasall::knowledge::KnowledgeHealthSnapshot;
using dasall::knowledge::KnowledgeQuery;
using dasall::knowledge::KnowledgeQueryKind;
using dasall::knowledge::KnowledgeTelemetryEvent;
using dasall::knowledge::RetrievalMode;
using dasall::knowledge::facade::KnowledgeServiceDeps;
using dasall::knowledge::facade::KnowledgeServiceFacade;
using dasall::knowledge::query::NormalizeResult;
using dasall::knowledge::query::NormalizedQuery;
using dasall::knowledge::query::RetrievalPlan;
using dasall::knowledge::query::RoutePlanResult;
using dasall::knowledge::rerank::RankedHit;
using dasall::knowledge::rerank::RankedHitSet;
using dasall::knowledge::retrieve::RecallCandidateSet;
using dasall::knowledge::retrieve::RecallCoordinatorResult;
using dasall::knowledge::retrieve::RecallHit;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] KnowledgeConfigSnapshot make_config(std::string profile_id) {
  KnowledgeConfigSnapshot config;
  config.knowledge_enabled = true;
  config.vector_enabled = true;
  config.retrieval_mode_default = RetrievalMode::LexicalOnly;
  config.profile_id = std::move(profile_id);
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
  query.request_id = "req-telemetry-fields";
  query.query_text = "DeepSeek Chat";
  query.query_kind = KnowledgeQueryKind::FactLookup;
  query.allowed_corpora = {"adr_normative"};
  query.top_k = 3U;
  query.max_context_projection_items = 3U;
  return query;
}

[[nodiscard]] KnowledgeServiceDeps make_success_deps(
    std::vector<KnowledgeTelemetryEvent>* telemetry_events) {
  KnowledgeServiceDeps deps;
  deps.now_ms = [] {
    return 1000;
  };
  deps.normalize_query = [](const KnowledgeQuery& query) {
    NormalizeResult result;
    NormalizedQuery normalized_query;
    normalized_query.request_id = query.request_id;
    normalized_query.normalized_text = query.query_text;
    normalized_query.lexical_terms = {"deepseek", "chat"};
    normalized_query.allowed_corpora = {"adr_normative"};
    normalized_query.query_kind = query.query_kind;
    normalized_query.top_k = query.top_k;
    normalized_query.max_context_projection_items = query.max_context_projection_items;
    normalized_query.prefer_exact_match = true;
    normalized_query.warnings = {"query_text_trimmed"};
    result.ok = true;
    result.normalized_query = std::move(normalized_query);
    return result;
  };
  deps.catalog_snapshot = [] {
    return dasall::knowledge::index::CorpusCatalogSnapshot();
  };
  deps.current_manifest = [] {
    return std::optional<IndexManifest>(IndexManifest{
        .format_version = 1U,
        .lexical_backend = "sqlite_fts5",
        .tokenizer_profile = "porter unicode61 remove_diacritics 1",
        .snapshot_id = "snapshot-telemetry-fields",
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
        .sparse_top_k = 3U,
        .allow_partial_results = false,
        .max_projection_items = 3U,
        .route_reason_codes = {"route_ok"},
    };
    result.route_reason_codes = {"route_ok"};
    return result;
  };
  deps.recall = [](const auto&) {
    RecallCoordinatorResult result;
    result.ok = true;
    result.candidates.sparse_succeeded = true;
    result.candidates.warnings = {"dense_lane_skipped_default_lexical"};
    result.candidates.sparse_hits = {RecallHit{
        .corpus_id = "adr_normative",
        .document_id = "adr-001",
        .chunk_id = "chunk-001",
        .score = 0.9F,
        .raw_snippet = "DeepSeek Chat provider profile evidence",
        .citation_ref = "ADR-001#deepseek",
        .updated_at = 1713657600000,
        .authority_level = AuthorityLevel::Normative,
        .tags = {"normative"},
    }};
    return result;
  };
  deps.rerank = [](const RecallCandidateSet& candidates, const FreshnessSnapshot&, const auto&) {
    RankedHitSet ranked;
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
        .evidence_id = "evidence-telemetry-fields",
        .snippet = "DeepSeek Chat provider profile evidence",
        .citation_ref = "ADR-001#deepseek",
        .confidence = 1.0F,
        .freshness = FreshnessState::Fresh,
        .tags = {"normative"},
    }};
    bundle.context_projection = {"[provider] DeepSeek Chat provider profile evidence"};
    return bundle;
  };
  deps.collect_health_snapshot = [] {
    KnowledgeHealthSnapshot snapshot;
    snapshot.state = HealthState::Healthy;
    snapshot.active_snapshot_id = "snapshot-telemetry-fields";
    snapshot.freshness_state = FreshnessState::Fresh;
    snapshot.vector_backend_available = true;
    snapshot.last_known_good_available = true;
    return snapshot;
  };
  deps.emit_retrieve_event = [telemetry_events](const KnowledgeTelemetryEvent& event) {
    telemetry_events->push_back(event);
  };
  return deps;
}

void test_retrieve_telemetry_prefers_query_profile_id_when_present() {
  std::vector<KnowledgeTelemetryEvent> telemetry_events;
  KnowledgeServiceFacade facade(make_success_deps(&telemetry_events));

  assert_true(facade.init(make_config("desktop_full")),
              "telemetry fields test should initialize facade with a consistent config");

  auto query = make_query();
  query.profile_id = std::string("edge_balanced");
  const auto result = facade.retrieve(query);

  assert_true(result.ok, "telemetry fields test should keep retrieve successful");
  assert_equal(1, static_cast<int>(telemetry_events.size()),
               "telemetry fields test should emit exactly one retrieve event");
  assert_equal(std::string("edge_balanced"), telemetry_events.front().profile_id,
               "retrieve telemetry should prefer the explicit query profile_id");
  assert_true(telemetry_events.front().vector_backend_ready,
              "retrieve telemetry should surface vector backend readiness");
  assert_equal(1, static_cast<int>(telemetry_events.front().sparse_hit_count),
               "retrieve telemetry should surface sparse lane hit count");
  assert_equal(0, static_cast<int>(telemetry_events.front().dense_hit_count),
               "retrieve telemetry should surface dense lane hit count even when zero");
  assert_equal(2, static_cast<int>(telemetry_events.front().warning_count),
               "retrieve telemetry should preserve the total warning count");
  assert_true(telemetry_events.front().warning_summary ==
                  std::vector<std::string>{"query_text_trimmed",
                                           "dense_lane_skipped_default_lexical"},
              "retrieve telemetry should aggregate warning summary from normalize and recall stages");
}

void test_retrieve_telemetry_falls_back_to_config_profile_id() {
  std::vector<KnowledgeTelemetryEvent> telemetry_events;
  KnowledgeServiceFacade facade(make_success_deps(&telemetry_events));

  assert_true(facade.init(make_config("desktop_full")),
              "telemetry fields test should initialize facade with a consistent config");

  const auto result = facade.retrieve(make_query());

  assert_true(result.ok, "telemetry fields test should keep retrieve successful");
  assert_equal(1, static_cast<int>(telemetry_events.size()),
               "telemetry fields test should emit exactly one retrieve event");
  assert_equal(std::string("desktop_full"), telemetry_events.front().profile_id,
               "retrieve telemetry should fall back to the initialized config profile_id");
  assert_true(telemetry_events.front().vector_backend_ready,
              "retrieve telemetry should keep vector backend readiness when falling back to config profile_id");
}

}  // namespace

int main() {
  try {
    test_retrieve_telemetry_prefers_query_profile_id_when_present();
    test_retrieve_telemetry_falls_back_to_config_profile_id();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}