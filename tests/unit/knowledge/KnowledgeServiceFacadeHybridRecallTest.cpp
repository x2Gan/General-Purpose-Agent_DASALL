#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "evidence/EvidenceAssembler.h"
#include "facade/KnowledgeService.h"
#include "health/FreshnessController.h"
#include "index/CorpusCatalog.h"
#include "index/IndexReader.h"
#include "query/CorpusRouter.h"
#include "query/QueryNormalizer.h"
#include "rerank/Reranker.h"
#include "retrieve/RecallCoordinator.h"
#include "retrieve/SparseRetriever.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::IndexManifest;
using dasall::knowledge::KnowledgeConfigSnapshot;
using dasall::knowledge::KnowledgeQuery;
using dasall::knowledge::KnowledgeQueryKind;
using dasall::knowledge::RetrievalMode;
using dasall::knowledge::SourceFormat;
using dasall::knowledge::SourceKind;
using dasall::knowledge::TrustLevel;
using dasall::knowledge::evidence::EvidenceAssembler;
using dasall::knowledge::facade::KnowledgeServiceDeps;
using dasall::knowledge::facade::KnowledgeServiceFacade;
using dasall::knowledge::index::CorpusCatalog;
using dasall::knowledge::index::IndexReader;
using dasall::knowledge::index::IndexSnapshot;
using dasall::knowledge::query::CorpusRouter;
using dasall::knowledge::query::QueryNormalizer;
using dasall::knowledge::query::QueryNormalizePolicy;
using dasall::knowledge::rerank::Reranker;
using dasall::knowledge::retrieve::DenseRecallResult;
using dasall::knowledge::retrieve::RecallCoordinator;
using dasall::knowledge::retrieve::RecallCoordinatorDeps;
using dasall::knowledge::retrieve::RecallCoordinatorPolicy;
using dasall::knowledge::retrieve::RecallHit;
using dasall::knowledge::retrieve::SparseIndexSearchRequest;
using dasall::knowledge::retrieve::SparseIndexSearchResult;
using dasall::knowledge::retrieve::SparseRetrieveRequest;
using dasall::knowledge::retrieve::SparseRetrieveResult;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] KnowledgeConfigSnapshot make_config() {
  KnowledgeConfigSnapshot config;
  config.knowledge_enabled = true;
  config.vector_enabled = true;
  config.retrieval_mode_default = RetrievalMode::Hybrid;
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
  query.request_id = "req-hybrid";
  query.query_text = "policy evidence";
  query.query_kind = KnowledgeQueryKind::PolicyEvidence;
  query.domain_tags = {"ops"};
  query.allowed_corpora = {"policy_hybrid"};
  query.top_k = 4U;
  query.max_context_projection_items = 4U;
  return query;
}

[[nodiscard]] dasall::knowledge::CorpusDescriptor make_descriptor() {
  dasall::knowledge::CorpusDescriptor descriptor;
  descriptor.corpus_id = "policy_hybrid";
  descriptor.display_name = "Policy Hybrid";
  descriptor.source_uri = "docs/policy/";
  descriptor.trust_level = TrustLevel::Trusted;
  descriptor.authority_level = AuthorityLevel::Normative;
  descriptor.source_kind = SourceKind::File;
  descriptor.allowed_formats = {SourceFormat::Markdown};
  descriptor.include_globs = {"*.md"};
  descriptor.exclude_globs = {};
  descriptor.supported_modes = {RetrievalMode::Hybrid};
  descriptor.active_snapshot_id = "snapshot-hybrid-001";
  descriptor.last_updated_ms = 1713657600000;
  descriptor.tags = {"ops"};
  descriptor.metadata = {
      {"baseline_class", "trusted_corpus"},
      {"owner_module", "knowledge"},
      {"refresh_strategy", "manual"},
      {"default_language", "zh-CN"},
  };
  return descriptor;
}

[[nodiscard]] IndexManifest make_manifest() {
  IndexManifest manifest;
  manifest.tokenizer_profile = "porter unicode61 remove_diacritics 1";
  manifest.snapshot_id = "snapshot-hybrid-001";
  manifest.built_at = 1713657599000;
  manifest.effective_at = 1713657600000;
  manifest.document_count = 2U;
  manifest.chunk_count = 2U;
  manifest.vector_enabled = true;
  return manifest;
}

[[nodiscard]] std::shared_ptr<const IndexSnapshot> make_snapshot() {
  auto snapshot = std::make_shared<IndexSnapshot>();
  snapshot->manifest = make_manifest();
  snapshot->checksum = "checksum-hybrid-001";
  snapshot->search = [](const SparseIndexSearchRequest&) {
    SparseIndexSearchResult result;
    result.ok = true;
    return result;
  };
  return snapshot;
}

[[nodiscard]] RecallHit make_hit(std::string chunk_id,
                                 float score,
                                 std::string snippet,
                                 std::string citation_ref) {
  RecallHit hit;
  hit.corpus_id = "policy_hybrid";
  hit.document_id = "policy-doc";
  hit.chunk_id = std::move(chunk_id);
  hit.score = score;
  hit.raw_snippet = std::move(snippet);
  hit.citation_ref = std::move(citation_ref);
  hit.updated_at = 1713657600000;
  hit.authority_level = AuthorityLevel::Normative;
  hit.tags = {"ops"};
  return hit;
}

void test_facade_runs_hybrid_recall_via_real_component_owners() {
  auto corpus_catalog = std::make_unique<CorpusCatalog>();
  assert_true(corpus_catalog->replace_all({make_descriptor()}),
              "catalog bootstrap should succeed for the hybrid facade test");

  RecallCoordinatorDeps recall_deps;
  recall_deps.sparse_lane = [](const SparseRetrieveRequest&) {
    SparseRetrieveResult result;
    result.ok = true;
    result.hits = {make_hit("chunk-sparse", 0.82F, "policy sparse evidence", "DOC#sparse")};
    return result;
  };
  recall_deps.dense_lane = [](const auto&) {
    DenseRecallResult result;
    result.ok = true;
    result.hits = {make_hit("chunk-dense", 0.79F, "policy dense evidence", "DOC#dense")};
    return result;
  };

  KnowledgeServiceDeps deps;
  deps.query_normalizer = std::make_unique<QueryNormalizer>(QueryNormalizePolicy{});
  deps.corpus_catalog = std::move(corpus_catalog);
  deps.index_reader = std::make_unique<IndexReader>(make_snapshot());
  deps.freshness_controller = std::make_unique<dasall::knowledge::FreshnessController>();
  deps.corpus_router = std::make_unique<CorpusRouter>();
  deps.recall_coordinator = std::make_unique<RecallCoordinator>(
      std::move(recall_deps),
      RecallCoordinatorPolicy{
          .max_parallel_recall = 1U,
          .sparse_lane_timeout_ms = 350,
          .dense_lane_timeout_ms = 350,
      });
  deps.reranker = std::make_unique<Reranker>();
  deps.evidence_assembler = std::make_unique<EvidenceAssembler>();
  deps.now_ms = [] { return 1713657601000LL; };

  KnowledgeServiceFacade facade(std::move(deps));

  assert_true(facade.init(make_config()),
              "facade should initialize before running the hybrid recall path");
  const auto result = facade.retrieve(make_query());
  assert_true(result.ok,
              "real component owners should drive a successful hybrid retrieve path");
  assert_true(result.has_consistent_values(),
              "hybrid retrieve result should preserve public result consistency");
  assert_equal(static_cast<int>(RetrievalMode::Hybrid), static_cast<int>(result.mode),
               "corpus router should select hybrid mode for a dense-capable policy corpus");
  assert_true(result.evidence.has_value(),
              "successful hybrid retrieve should expose assembled evidence");
  assert_equal(2, static_cast<int>(result.evidence->slices.size()),
               "hybrid retrieve should carry both sparse and dense evidence slices");
  assert_true(!result.evidence->degraded,
              "fully successful hybrid retrieve should not be marked degraded");
}

}  // namespace

int main() {
  try {
    test_facade_runs_hybrid_recall_via_real_component_owners();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}