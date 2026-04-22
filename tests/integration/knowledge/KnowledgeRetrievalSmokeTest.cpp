#include <sqlite3.h>

#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "IKnowledgeService.h"
#include "KnowledgeTypes.h"
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
using dasall::knowledge::CorpusDescriptor;
using dasall::knowledge::EvidenceBundle;
using dasall::knowledge::FreshnessController;
using dasall::knowledge::IndexManifest;
using dasall::knowledge::KnowledgeConfigSnapshot;
using dasall::knowledge::KnowledgeQuery;
using dasall::knowledge::KnowledgeQueryKind;
using dasall::knowledge::RefreshResult;
using dasall::knowledge::RefreshStatus;
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
using dasall::knowledge::query::QueryNormalizePolicy;
using dasall::knowledge::query::QueryNormalizer;
using dasall::knowledge::rerank::Reranker;
using dasall::knowledge::retrieve::DenseRecallRequest;
using dasall::knowledge::retrieve::DenseRecallResult;
using dasall::knowledge::retrieve::RecallCoordinator;
using dasall::knowledge::retrieve::RecallCoordinatorDeps;
using dasall::knowledge::retrieve::RecallCoordinatorPolicy;
using dasall::knowledge::retrieve::SparseIndexSearchRequest;
using dasall::knowledge::retrieve::SparseIndexSearchResult;
using dasall::knowledge::retrieve::SparseRetriever;
using dasall::knowledge::retrieve::SparseRetrieverDeps;
using dasall::knowledge::retrieve::SparseSearchRow;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

void execute_sql(sqlite3* connection, const std::string& sql) {
  char* error_message = nullptr;
  const int sqlite_status =
      sqlite3_exec(connection, sql.c_str(), nullptr, nullptr, &error_message);
  if (sqlite_status != SQLITE_OK) {
    const std::string message =
        error_message != nullptr ? error_message : "failed to execute sqlite statement";
    sqlite3_free(error_message);
    throw std::runtime_error(message);
  }

  sqlite3_free(error_message);
}

[[nodiscard]] std::string encode_tags(const std::vector<std::string>& tags) {
  std::string encoded = "|";
  for (const auto& tag : tags) {
    encoded += tag;
    encoded.push_back('|');
  }
  return encoded;
}

[[nodiscard]] std::vector<std::string> decode_tags(const std::string& encoded_tags) {
  std::vector<std::string> tags;
  std::string current_tag;
  for (const char character : encoded_tags) {
    if (character == '|') {
      if (!current_tag.empty()) {
        tags.push_back(current_tag);
        current_tag.clear();
      }
      continue;
    }

    current_tag.push_back(character);
  }

  if (!current_tag.empty()) {
    tags.push_back(std::move(current_tag));
  }

  return tags;
}

class SqliteLexicalIndexFixture {
 public:
  SqliteLexicalIndexFixture() {
    if (sqlite3_open(":memory:", &connection_) != SQLITE_OK) {
      throw std::runtime_error("failed to open sqlite in-memory database");
    }

    execute_sql(connection_,
                "CREATE VIRTUAL TABLE chunks_fts USING fts5("
                "corpus_id UNINDEXED,"
                "document_id UNINDEXED,"
                "chunk_id UNINDEXED,"
                "chunk_text,"
                "citation_ref UNINDEXED,"
                "updated_at UNINDEXED,"
                "authority_level UNINDEXED,"
                "language UNINDEXED,"
                "tags UNINDEXED,"
                "tokenize='porter unicode61 remove_diacritics 1'"
                ");");
  }

  ~SqliteLexicalIndexFixture() {
    if (connection_ != nullptr) {
      sqlite3_close(connection_);
    }
  }

  void insert_row(std::string corpus_id,
                  std::string document_id,
                  std::string chunk_id,
                  std::string chunk_text,
                  std::string citation_ref,
                  std::int64_t updated_at,
                  AuthorityLevel authority_level,
                  std::string language,
                  std::vector<std::string> tags) {
    constexpr auto insert_sql =
        "INSERT INTO chunks_fts("
        "corpus_id, document_id, chunk_id, chunk_text, citation_ref, updated_at, authority_level, language, tags"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9);";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection_, insert_sql, -1, &statement, nullptr) != SQLITE_OK) {
      throw std::runtime_error("failed to prepare lexical fixture insert");
    }

    sqlite3_bind_text(statement, 1, corpus_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, document_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, chunk_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, chunk_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 5, citation_ref.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 6, updated_at);
    sqlite3_bind_int(statement, 7, static_cast<int>(authority_level));
    sqlite3_bind_text(statement, 8, language.c_str(), -1, SQLITE_TRANSIENT);
    const auto encoded_tags = encode_tags(tags);
    sqlite3_bind_text(statement, 9, encoded_tags.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(statement) != SQLITE_DONE) {
      sqlite3_finalize(statement);
      throw std::runtime_error("failed to insert lexical fixture row");
    }

    sqlite3_finalize(statement);
  }

  [[nodiscard]] SparseIndexSearchResult search(
      const SparseIndexSearchRequest& request) const {
    std::string sql =
        "SELECT corpus_id, document_id, chunk_id, chunk_text, citation_ref, updated_at, authority_level, language, tags, bm25(chunks_fts) "
        "FROM chunks_fts WHERE chunks_fts MATCH ?1";

    int bind_index = 2;
    if (!request.allowed_corpus_ids.empty()) {
      sql += " AND corpus_id IN (";
      for (std::size_t index = 0U; index < request.allowed_corpus_ids.size(); ++index) {
        if (index > 0U) {
          sql += ", ";
        }
        sql += "?" + std::to_string(bind_index++);
      }
      sql += ")";
    }

    sql += " AND CAST(authority_level AS INTEGER) <= ?" + std::to_string(bind_index++);

    if (request.required_language.has_value()) {
      sql += " AND language = ?" + std::to_string(bind_index++);
    }

    for (std::size_t index = 0U; index < request.required_tags.size(); ++index) {
      sql += " AND instr(tags, ?" + std::to_string(bind_index++) + ") > 0";
    }

    sql += " ORDER BY bm25(chunks_fts) LIMIT ?" + std::to_string(bind_index++);

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection_, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
      SparseIndexSearchResult result;
      result.ok = false;
      result.error = dasall::knowledge::make_knowledge_error_info(
          dasall::knowledge::KnowledgeErrorCode::IndexUnavailable,
          "failed to prepare lexical fixture search statement",
          "knowledge_retrieval_smoke.search");
      return result;
    }

    int parameter_index = 1;
    sqlite3_bind_text(statement,
                      parameter_index++,
                      request.expression.match_expression.c_str(),
                      -1,
                      SQLITE_TRANSIENT);

    for (const auto& corpus_id : request.allowed_corpus_ids) {
      sqlite3_bind_text(statement,
                        parameter_index++,
                        corpus_id.c_str(),
                        -1,
                        SQLITE_TRANSIENT);
    }

    sqlite3_bind_int(statement,
                     parameter_index++,
                     static_cast<int>(request.minimum_authority_level));

    if (request.required_language.has_value()) {
      sqlite3_bind_text(statement,
                        parameter_index++,
                        request.required_language->c_str(),
                        -1,
                        SQLITE_TRANSIENT);
    }

    for (const auto& required_tag : request.required_tags) {
      const auto encoded_tag = "|" + required_tag + "|";
      sqlite3_bind_text(statement,
                        parameter_index++,
                        encoded_tag.c_str(),
                        -1,
                        SQLITE_TRANSIENT);
    }

    sqlite3_bind_int(statement, parameter_index++, static_cast<int>(request.top_k));

    SparseIndexSearchResult result;
    result.ok = true;
    while (sqlite3_step(statement) == SQLITE_ROW) {
      const auto score = std::max(0.0F, static_cast<float>(-sqlite3_column_double(statement, 9)));
      const auto* language_text =
          reinterpret_cast<const char*>(sqlite3_column_text(statement, 7));
      const auto* tags_text = reinterpret_cast<const char*>(sqlite3_column_text(statement, 8));
      result.rows.push_back(SparseSearchRow{
          .corpus_id = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)),
          .document_id = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1)),
          .chunk_id = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2)),
          .score = score,
          .chunk_text = reinterpret_cast<const char*>(sqlite3_column_text(statement, 3)),
          .citation_ref = reinterpret_cast<const char*>(sqlite3_column_text(statement, 4)),
          .updated_at = sqlite3_column_int64(statement, 5),
          .authority_level = static_cast<AuthorityLevel>(sqlite3_column_int(statement, 6)),
          .language = language_text != nullptr ? std::optional<std::string>(language_text)
                                               : std::nullopt,
          .tags = tags_text != nullptr ? decode_tags(tags_text) : std::vector<std::string>{},
      });
    }

    sqlite3_finalize(statement);
    return result;
  }

 private:
  sqlite3* connection_ = nullptr;
};

[[nodiscard]] QueryNormalizePolicy make_normalize_policy() {
  QueryNormalizePolicy policy;
  policy.max_query_text_bytes = 512U;
  policy.max_lexical_terms = 16U;
  policy.max_top_k = 8U;
  policy.max_context_projection_items = 6U;
  policy.allowed_domain_tags = {"normative"};
  policy.allowed_corpora = {"adr-normative"};
  return policy;
}

[[nodiscard]] KnowledgeConfigSnapshot make_config() {
  KnowledgeConfigSnapshot config;
  config.knowledge_enabled = true;
  config.vector_enabled = false;
  config.retrieval_mode_default = RetrievalMode::LexicalOnly;
  config.evidence_budget_tokens = 512U;
  config.max_context_projection_items = 4U;
  config.catalog_refresh_interval_ms = 30000;
  config.catalog_expire_after_ms = 120000;
  config.allow_stale_read = false;
  config.failure_backoff_ms = 1000;
  config.request_deadline_ms = 1000;
  config.allow_budget_degrade = true;
  config.max_parallel_recall = 1U;
  config.sparse_recall_timeout_ms = 350;
  config.dense_recall_timeout_ms = 350;
  config.ingest_timeout_ms = 2000;
  return config;
}

[[nodiscard]] KnowledgeQuery make_query() {
  KnowledgeQuery query;
  query.request_id = "req-knowledge-smoke-027";
  query.session_id = "session-knowledge-smoke-027";
  query.query_text = "Policy boundary owner contract";
  query.query_kind = KnowledgeQueryKind::PolicyEvidence;
  query.domain_tags = {"normative"};
  query.allowed_corpora = {"adr-normative"};
  query.top_k = 4U;
  query.max_context_projection_items = 2U;
  return query;
}

[[nodiscard]] CorpusDescriptor make_descriptor(std::string default_language = "zh-CN") {
  CorpusDescriptor descriptor;
  descriptor.corpus_id = "adr-normative";
  descriptor.display_name = "ADR Normative";
  descriptor.source_uri = "docs/adr/adr-normative.md";
  descriptor.trust_level = TrustLevel::Trusted;
  descriptor.authority_level = AuthorityLevel::Normative;
  descriptor.source_kind = SourceKind::File;
  descriptor.allowed_formats = {SourceFormat::Markdown};
  descriptor.include_globs = {"*.md"};
  descriptor.exclude_globs = {"archive/*.md"};
  descriptor.supported_modes = {RetrievalMode::LexicalOnly};
  descriptor.active_snapshot_id = "snapshot-knowledge-smoke-027";
  descriptor.last_updated_ms = 9000;
  descriptor.tags = {"normative"};
  descriptor.metadata = {
      {"baseline_class", "knowledge"},
      {"owner_module", "knowledge"},
      {"refresh_strategy", "manual"},
      {"default_language", std::move(default_language)},
  };
  return descriptor;
}

[[nodiscard]] IndexManifest make_manifest() {
  IndexManifest manifest;
  manifest.format_version = 1U;
  manifest.lexical_backend = "sqlite_fts5";
  manifest.tokenizer_profile = "porter unicode61 remove_diacritics 1";
  manifest.snapshot_id = "snapshot-knowledge-smoke-027";
  manifest.built_at = 8000;
  manifest.effective_at = 9000;
  manifest.document_count = 1U;
  manifest.chunk_count = 1U;
  manifest.vector_enabled = false;
  return manifest;
}

struct KnowledgeSmokeHarness {
  SqliteLexicalIndexFixture sqlite_index;
  CorpusCatalog corpus_catalog;
  QueryNormalizer query_normalizer;
  FreshnessController freshness_controller;
  CorpusRouter corpus_router;
  IndexReader index_reader;
  SparseRetriever sparse_retriever;
  RecallCoordinator recall_coordinator;
  Reranker reranker;
  EvidenceAssembler evidence_assembler;
  KnowledgeConfigSnapshot config;
  std::int64_t now_ms = 12000;
  std::unique_ptr<dasall::knowledge::IKnowledgeService> knowledge_service;

  explicit KnowledgeSmokeHarness(std::string corpus_language = "zh-CN")
      : query_normalizer(make_normalize_policy()),
        sparse_retriever(SparseRetrieverDeps{
            .search_index = [this](const SparseIndexSearchRequest& request) {
              return index_reader.search_sparse(request);
            },
        }),
        recall_coordinator(RecallCoordinatorDeps{
                               .sparse_lane = [this](const dasall::knowledge::retrieve::SparseRetrieveRequest& request) {
                                 return sparse_retriever.retrieve(request);
                               },
                               .dense_bridge = nullptr,
                               .dense_lane = [](const DenseRecallRequest&) {
                                 DenseRecallResult result;
                                 result.ok = false;
                                 result.failure_reason_codes = {"lane_unavailable"};
                                 return result;
                               },
                           },
                           RecallCoordinatorPolicy{
                               .max_parallel_recall = 1U,
                               .sparse_lane_timeout_ms = 350,
                               .dense_lane_timeout_ms = 350,
                           }),
        config(make_config()) {
    sqlite_index.insert_row(
        "adr-normative",
        "ADR-0001",
        "chunk-0001",
        "Policy boundary owner contract defines the recovery requirement. Operators must preserve the owner boundary during recovery.",
        "ADR-0001#policy",
        9500,
        AuthorityLevel::Normative,
      corpus_language,
        {"normative"});

    const bool catalog_replaced = corpus_catalog.replace_all({make_descriptor(corpus_language)});
    assert_true(catalog_replaced,
                "knowledge smoke harness should install a consistent corpus descriptor");

    auto snapshot = std::make_shared<IndexSnapshot>();
    snapshot->manifest = make_manifest();
    snapshot->checksum = "checksum-knowledge-smoke-027";
    snapshot->search = [this](const SparseIndexSearchRequest& request) {
      return sqlite_index.search(request);
    };

    const bool snapshot_swapped = index_reader.swap_active_snapshot(snapshot);
    assert_true(snapshot_swapped,
                "knowledge smoke harness should install an active lexical snapshot");

    auto service = std::make_unique<KnowledgeServiceFacade>(make_service_deps());
    assert_true(service->init(config),
                "knowledge smoke harness should initialize the facade with a consistent config");
    knowledge_service = std::move(service);
  }

  [[nodiscard]] KnowledgeServiceDeps make_service_deps() {
    KnowledgeServiceDeps deps;
    deps.now_ms = [this] { return now_ms; };
    deps.normalize_query = [this](const KnowledgeQuery& query) {
      return query_normalizer.normalize(query);
    };
    deps.catalog_snapshot = [this] { return corpus_catalog.snapshot(); };
    deps.current_manifest = [this] { return index_reader.current_manifest(); };
    deps.evaluate_freshness = [this](const std::optional<IndexManifest>& manifest,
                                     const KnowledgeConfigSnapshot& service_config,
                                     std::int64_t current_time_ms,
                                     bool query_allow_stale) {
      return freshness_controller.evaluate(manifest,
                                           service_config,
                                           current_time_ms,
                                           query_allow_stale);
    };
    deps.build_plan = [this](const dasall::knowledge::query::NormalizedQuery& query,
                             const KnowledgeConfigSnapshot& service_config,
                             const dasall::knowledge::index::CorpusCatalogSnapshot& catalog,
                             const dasall::knowledge::FreshnessSnapshot& freshness) {
      return corpus_router.build_plan(query, service_config, catalog, freshness);
    };
    deps.recall = [this](const dasall::knowledge::retrieve::RecallRequest& request) {
      return recall_coordinator.recall(request);
    };
    deps.rerank = [this](const dasall::knowledge::retrieve::RecallCandidateSet& candidates,
                         const dasall::knowledge::FreshnessSnapshot& freshness,
                         const dasall::knowledge::rerank::RerankPolicy& policy) {
      return reranker.rerank(candidates, freshness, policy);
    };
    deps.assemble_evidence = [this](const dasall::knowledge::rerank::RankedHitSet& hits,
                                    const dasall::knowledge::evidence::EvidenceAssemblePolicy& policy) {
      return evidence_assembler.assemble(hits, policy);
    };
    deps.request_refresh = [](const dasall::knowledge::CorpusChangeSet&) {
      RefreshResult result;
      result.status = RefreshStatus::Busy;
      return result;
    };
    return deps;
  }
};

[[nodiscard]] EvidenceBundle retrieve_runtime_projection(dasall::knowledge::IKnowledgeService& service,
                                                         const KnowledgeQuery& query) {
  const auto result = service.retrieve(query);
  assert_true(result.ok,
              "knowledge retrieval smoke should complete successfully through the service interface");
  assert_true(result.has_consistent_values(),
              "knowledge retrieval smoke should preserve retrieve result invariants");
  assert_true(result.mode == RetrievalMode::LexicalOnly,
              "knowledge retrieval smoke should stay on the lexical-only route");
  assert_true(result.evidence.has_value(),
              "knowledge retrieval smoke should return an evidence bundle");
  return *result.evidence;
}

void test_knowledge_retrieval_smoke_returns_non_empty_context_projection() {
  KnowledgeSmokeHarness harness;
  const auto evidence = retrieve_runtime_projection(*harness.knowledge_service, make_query());

  assert_true(!evidence.context_projection.empty(),
              "knowledge retrieval smoke should return at least one context projection line");
  assert_equal(1, static_cast<int>(evidence.context_projection.size()),
               "knowledge retrieval smoke should keep a single projection line for the single lexical hit");
  assert_true(evidence.context_projection.front().find("[normative]") != std::string::npos,
              "knowledge retrieval smoke projection should surface the normative authority label");
  assert_true(evidence.context_projection.front().find("ADR-0001#policy") != std::string::npos,
              "knowledge retrieval smoke projection should retain the citation ref");
}

void test_knowledge_retrieval_smoke_keeps_en_corpus_retrievable_for_session_queries() {
  KnowledgeSmokeHarness harness("en");
  const auto evidence = retrieve_runtime_projection(*harness.knowledge_service, make_query());

  assert_true(!evidence.context_projection.empty(),
              "session-scoped queries should not implicitly filter out english corpora");
  assert_true(evidence.context_projection.front().find("ADR-0001#policy") != std::string::npos,
              "english corpus retrieval should still preserve the citation ref");
}

}  // namespace

int main() {
  try {
    test_knowledge_retrieval_smoke_returns_non_empty_context_projection();
    test_knowledge_retrieval_smoke_keeps_en_corpus_retrievable_for_session_queries();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}