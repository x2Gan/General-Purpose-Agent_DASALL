#include <sqlite3.h>

#include <algorithm>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "IKnowledgeService.h"
#include "KnowledgeErrors.h"
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
#include "retrieve/VectorRetrieverBridge.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::CorpusChangeSet;
using dasall::knowledge::CorpusDescriptor;
using dasall::knowledge::EvidenceBundle;
using dasall::knowledge::FreshnessController;
using dasall::knowledge::FreshnessSnapshot;
using dasall::knowledge::IndexManifest;
using dasall::knowledge::KnowledgeConfigSnapshot;
using dasall::knowledge::KnowledgeErrorCode;
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
using dasall::knowledge::query::NormalizeResult;
using dasall::knowledge::query::NormalizedQuery;
using dasall::knowledge::query::QueryNormalizePolicy;
using dasall::knowledge::query::QueryNormalizer;
using dasall::knowledge::query::RoutePlanResult;
using dasall::knowledge::rerank::Reranker;
using dasall::knowledge::retrieve::DenseQueryInputMode;
using dasall::knowledge::retrieve::DenseQueryRequest;
using dasall::knowledge::retrieve::DenseRecallRequest;
using dasall::knowledge::retrieve::DenseRecallResult;
using dasall::knowledge::retrieve::IQueryEncoder;
using dasall::knowledge::retrieve::IVectorRecallStore;
using dasall::knowledge::retrieve::RecallCoordinator;
using dasall::knowledge::retrieve::RecallCoordinatorDeps;
using dasall::knowledge::retrieve::RecallCoordinatorPolicy;
using dasall::knowledge::retrieve::RecallCoordinatorResult;
using dasall::knowledge::retrieve::RecallRequest;
using dasall::knowledge::retrieve::SparseIndexSearchRequest;
using dasall::knowledge::retrieve::SparseIndexSearchResult;
using dasall::knowledge::retrieve::SparseRetriever;
using dasall::knowledge::retrieve::SparseRetrieverDeps;
using dasall::knowledge::retrieve::SparseSearchRow;
using dasall::knowledge::retrieve::VectorRetrieverBridge;
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

void assert_has_reason_code(const std::vector<std::string>& reason_codes,
                            const std::string& reason_code,
                            const std::string& message) {
  assert_true(std::find(reason_codes.begin(), reason_codes.end(), reason_code) !=
                  reason_codes.end(),
              message);
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
          "knowledge_failure_degrade.search");
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

class UnavailableVectorStore final : public IVectorRecallStore {
 public:
  [[nodiscard]] bool available() const override {
    return false;
  }

  [[nodiscard]] DenseQueryInputMode query_input_mode() const override {
    return DenseQueryInputMode::TextOnly;
  }

  [[nodiscard]] std::vector<dasall::knowledge::retrieve::RecallHit> search(
      const DenseQueryRequest&) const override {
    return {};
  }
};

class UnusedQueryEncoder final : public IQueryEncoder {
 public:
  [[nodiscard]] std::vector<float> encode(std::string_view) const override {
    return {};
  }

  [[nodiscard]] bool available() const override {
    return false;
  }
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

[[nodiscard]] KnowledgeConfigSnapshot make_hybrid_config(bool allow_stale_read = true) {
  KnowledgeConfigSnapshot config;
  config.knowledge_enabled = true;
  config.vector_enabled = true;
  config.retrieval_mode_default = RetrievalMode::Hybrid;
  config.evidence_budget_tokens = 512U;
  config.max_context_projection_items = 4U;
  config.catalog_refresh_interval_ms = 30000;
  config.catalog_expire_after_ms = 120000;
  config.allow_stale_read = allow_stale_read;
  config.failure_backoff_ms = 1000;
  config.request_deadline_ms = 1000;
  config.allow_budget_degrade = true;
  config.max_parallel_recall = 1U;
  config.sparse_recall_timeout_ms = 350;
  config.dense_recall_timeout_ms = 350;
  config.ingest_timeout_ms = 2000;
  return config;
}

[[nodiscard]] KnowledgeQuery make_query(bool allow_stale = false) {
  KnowledgeQuery query;
  query.request_id = "req-knowledge-failure-028";
  query.session_id = "session-knowledge-failure-028";
  query.query_text = "Policy boundary owner contract";
  query.query_kind = KnowledgeQueryKind::PolicyEvidence;
  query.domain_tags = {"normative"};
  query.allowed_corpora = {"adr-normative"};
  query.top_k = 4U;
  query.max_context_projection_items = 2U;
  query.allow_stale = allow_stale;
  return query;
}

[[nodiscard]] CorpusDescriptor make_descriptor() {
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
  descriptor.supported_modes = {RetrievalMode::LexicalOnly, RetrievalMode::Hybrid};
  descriptor.active_snapshot_id = "snapshot-knowledge-failure-028";
  descriptor.last_updated_ms = 9000;
  descriptor.tags = {"normative"};
  descriptor.metadata = {
      {"baseline_class", "knowledge"},
      {"owner_module", "knowledge"},
      {"refresh_strategy", "manual"},
      {"default_language", "zh-CN"},
  };
  return descriptor;
}

[[nodiscard]] IndexManifest make_manifest(std::int64_t effective_at,
                                          bool vector_enabled = true) {
  IndexManifest manifest;
  manifest.format_version = 1U;
  manifest.lexical_backend = "sqlite_fts5";
  manifest.tokenizer_profile = "porter unicode61 remove_diacritics 1";
  manifest.snapshot_id = "snapshot-knowledge-failure-028";
  manifest.built_at = effective_at - 1000;
  manifest.effective_at = effective_at;
  manifest.document_count = 1U;
  manifest.chunk_count = 1U;
  manifest.vector_enabled = vector_enabled;
  return manifest;
}

[[nodiscard]] std::shared_ptr<const VectorRetrieverBridge> make_unavailable_dense_bridge() {
  return std::make_shared<VectorRetrieverBridge>(
      std::make_unique<UnusedQueryEncoder>(),
      std::make_unique<UnavailableVectorStore>());
}

struct HarnessOptions {
  KnowledgeConfigSnapshot config = make_hybrid_config();
  std::optional<IndexManifest> manifest = make_manifest(9000, true);
  std::int64_t now_ms = 12000;
  std::shared_ptr<const VectorRetrieverBridge> dense_bridge;
  std::function<DenseRecallResult(const DenseRecallRequest&)> dense_lane;
  std::function<RefreshResult(const CorpusChangeSet&)> request_refresh;
};

struct PreparedQuery {
  NormalizedQuery normalized_query;
  FreshnessSnapshot freshness;
  RoutePlanResult route_result;
};

struct FailureDegradeHarness {
  HarnessOptions options_;
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
  std::unique_ptr<dasall::knowledge::IKnowledgeService> knowledge_service;

  explicit FailureDegradeHarness(HarnessOptions options)
      : options_(std::move(options)),
        query_normalizer(make_normalize_policy()),
        sparse_retriever(SparseRetrieverDeps{
            .search_index = [this](const SparseIndexSearchRequest& request) {
              return index_reader.search_sparse(request);
            },
        }),
        recall_coordinator(RecallCoordinatorDeps{
                               .sparse_lane = [this](const dasall::knowledge::retrieve::SparseRetrieveRequest& request) {
                                 return sparse_retriever.retrieve(request);
                               },
                               .dense_bridge = options_.dense_bridge,
                               .dense_lane = options_.dense_lane,
                           },
                           RecallCoordinatorPolicy{
                               .max_parallel_recall = 1U,
                               .sparse_lane_timeout_ms = 350,
                               .dense_lane_timeout_ms = 350,
                           }) {
    sqlite_index.insert_row(
        "adr-normative",
        "ADR-0001",
        "chunk-0001",
        "Policy boundary owner contract defines the recovery requirement. Operators must preserve the owner boundary during recovery.",
        "ADR-0001#policy",
        9500,
        AuthorityLevel::Normative,
        "zh-CN",
        {"normative"});

    const bool catalog_replaced = corpus_catalog.replace_all({make_descriptor()});
    assert_true(catalog_replaced,
                "failure/degrade harness should install a consistent corpus descriptor");

    if (options_.manifest.has_value()) {
      auto snapshot = std::make_shared<IndexSnapshot>();
      snapshot->manifest = *options_.manifest;
      snapshot->checksum = "checksum-knowledge-failure-028";
      snapshot->search = [this](const SparseIndexSearchRequest& request) {
        return sqlite_index.search(request);
      };

      const bool snapshot_swapped = index_reader.swap_active_snapshot(snapshot);
      assert_true(snapshot_swapped,
                  "failure/degrade harness should install an active lexical snapshot");
    }

    auto service = std::make_unique<KnowledgeServiceFacade>(make_service_deps());
    assert_true(service->init(options_.config),
                "failure/degrade harness should initialize the facade with a consistent config");
    knowledge_service = std::move(service);
  }

  [[nodiscard]] PreparedQuery prepare(const KnowledgeQuery& query) {
    const auto normalize_result = query_normalizer.normalize(query);
    assert_true(normalize_result.ok && normalize_result.normalized_query.has_value(),
                "failure/degrade harness should normalize a consistent test query");

    const auto freshness = freshness_controller.evaluate(options_.manifest,
                                                         options_.config,
                                                         options_.now_ms,
                                                         query.allow_stale);
    assert_true(freshness.has_consistent_values(),
                "failure/degrade harness should produce a consistent freshness snapshot");

    const auto route_result = corpus_router.build_plan(*normalize_result.normalized_query,
                                                       options_.config,
                                                       corpus_catalog.snapshot(),
                                                       freshness);
    assert_true(route_result.has_consistent_values(),
                "failure/degrade harness should preserve route result invariants");

    return PreparedQuery{
        .normalized_query = *normalize_result.normalized_query,
        .freshness = freshness,
        .route_result = route_result,
    };
  }

  [[nodiscard]] RecallCoordinatorResult run_recall(const KnowledgeQuery& query) {
    const auto prepared = prepare(query);
    assert_true(prepared.route_result.ok && prepared.route_result.plan.has_value(),
                "failure/degrade recall helper requires a routable query");

    RecallRequest request;
    request.normalized_query = prepared.normalized_query;
    request.plan = *prepared.route_result.plan;
    request.required_language = std::string("zh-CN");
    return recall_coordinator.recall(request);
  }

  [[nodiscard]] dasall::knowledge::KnowledgeRetrieveResult retrieve(const KnowledgeQuery& query) {
    const auto result = knowledge_service->retrieve(query);
    assert_true(result.has_consistent_values(),
                "failure/degrade harness should preserve retrieve result invariants");
    return result;
  }

  [[nodiscard]] RefreshResult refresh(const CorpusChangeSet& changes) {
    const auto result = knowledge_service->request_refresh(changes);
    assert_true(result.has_consistent_values(),
                "failure/degrade harness should preserve refresh result invariants");
    return result;
  }

  [[nodiscard]] KnowledgeServiceDeps make_service_deps() {
    KnowledgeServiceDeps deps;
    deps.now_ms = [this] { return options_.now_ms; };
    deps.normalize_query = [this](const KnowledgeQuery& query) {
      return query_normalizer.normalize(query);
    };
    deps.catalog_snapshot = [this] { return corpus_catalog.snapshot(); };
    deps.current_manifest = [this] { return options_.manifest; };
    deps.evaluate_freshness = [this](const std::optional<IndexManifest>& manifest,
                                     const KnowledgeConfigSnapshot& service_config,
                                     std::int64_t current_time_ms,
                                     bool query_allow_stale) {
      return freshness_controller.evaluate(manifest,
                                           service_config,
                                           current_time_ms,
                                           query_allow_stale);
    };
    deps.build_plan = [this](const NormalizedQuery& query,
                             const KnowledgeConfigSnapshot& service_config,
                             const dasall::knowledge::index::CorpusCatalogSnapshot& catalog,
                             const FreshnessSnapshot& freshness) {
      return corpus_router.build_plan(query, service_config, catalog, freshness);
    };
    deps.recall = [this](const RecallRequest& request) {
      return recall_coordinator.recall(request);
    };
    deps.rerank = [this](const dasall::knowledge::retrieve::RecallCandidateSet& candidates,
                         const FreshnessSnapshot& freshness,
                         const dasall::knowledge::rerank::RerankPolicy& policy) {
      return reranker.rerank(candidates, freshness, policy);
    };
    deps.assemble_evidence = [this](const dasall::knowledge::rerank::RankedHitSet& hits,
                                    const dasall::knowledge::evidence::EvidenceAssemblePolicy& policy) {
      return evidence_assembler.assemble(hits, policy);
    };
    deps.request_refresh = [this](const CorpusChangeSet& changes) {
      if (options_.request_refresh) {
        return options_.request_refresh(changes);
      }

      RefreshResult result;
      result.status = RefreshStatus::Busy;
      return result;
    };
    return deps;
  }
};

void test_failure_degrade_integration_degrades_when_vector_backend_is_unavailable() {
  FailureDegradeHarness harness(HarnessOptions{
      .config = make_hybrid_config(),
      .manifest = make_manifest(9000, true),
      .now_ms = 12000,
      .dense_bridge = make_unavailable_dense_bridge(),
      .dense_lane = {},
      .request_refresh = {},
  });

  const auto query = make_query(false);
  const auto recall_result = harness.run_recall(query);
  assert_true(recall_result.ok,
              "vector-backend-unavailable hybrid recall should degrade instead of failing outright");
  assert_true(recall_result.candidates.degraded,
              "vector-backend-unavailable hybrid recall should mark candidates degraded");
  assert_has_reason_code(recall_result.candidates.warnings,
                         "dense_vector_backend_unavailable",
                         "vector-backend-unavailable hybrid recall should surface the dense unavailable warning code");

  const auto retrieve_result = harness.retrieve(query);
  assert_true(retrieve_result.ok,
              "vector-backend-unavailable hybrid retrieve should still succeed through lexical fallback");
  assert_true(retrieve_result.mode == RetrievalMode::Hybrid,
              "vector-backend-unavailable hybrid retrieve should preserve the routed hybrid mode");
  assert_true(retrieve_result.evidence.has_value(),
              "vector-backend-unavailable hybrid retrieve should still return evidence");
  assert_true(retrieve_result.evidence->degraded,
              "vector-backend-unavailable hybrid retrieve should propagate degraded evidence");
  assert_true(!retrieve_result.evidence->context_projection.empty(),
              "vector-backend-unavailable hybrid retrieve should keep lexical context projection");
}

void test_failure_degrade_integration_degrades_on_partial_dense_timeout() {
  FailureDegradeHarness harness(HarnessOptions{
      .config = make_hybrid_config(),
      .manifest = make_manifest(9000, true),
      .now_ms = 12000,
      .dense_bridge = nullptr,
      .dense_lane = [](const DenseRecallRequest&) {
        DenseRecallResult result;
        result.ok = false;
        result.failure_reason_codes = {"recall_timeout"};
        return result;
      },
      .request_refresh = {},
  });

  const auto query = make_query(false);
  const auto recall_result = harness.run_recall(query);
  assert_true(recall_result.ok,
              "partial dense timeout should degrade instead of failing when sparse lane succeeds");
  assert_true(recall_result.candidates.degraded,
              "partial dense timeout should mark candidates degraded");
  assert_has_reason_code(recall_result.candidates.warnings,
                         "dense_recall_timeout",
                         "partial dense timeout should surface the timeout warning code");

  const auto retrieve_result = harness.retrieve(query);
  assert_true(retrieve_result.ok,
              "partial dense timeout should still yield a successful retrieve result");
  assert_true(retrieve_result.evidence.has_value() && retrieve_result.evidence->degraded,
              "partial dense timeout should propagate degraded evidence to the service surface");
}

void test_failure_degrade_integration_rejects_stale_snapshot_without_query_opt_in() {
  FailureDegradeHarness harness(HarnessOptions{
      .config = make_hybrid_config(true),
      .manifest = make_manifest(9000, true),
      .now_ms = 70000,
      .dense_bridge = nullptr,
      .dense_lane = {},
      .request_refresh = {},
  });

  const auto result = harness.retrieve(make_query(false));
  assert_true(!result.ok,
              "stale snapshot without query opt-in should fail closed at retrieve surface");
  assert_true(result.error.has_value(),
              "stale snapshot rejection should surface an explicit error");
  assert_equal(static_cast<int>(KnowledgeErrorCode::IndexStaleRejected),
               result.error->details.code.value_or(-1),
               "stale snapshot rejection should map to IndexStaleRejected");
  assert_true(!result.evidence.has_value(),
              "stale snapshot rejection should not return any evidence bundle");
}

void test_failure_degrade_integration_returns_refresh_busy() {
  FailureDegradeHarness harness(HarnessOptions{
      .config = make_hybrid_config(),
      .manifest = make_manifest(9000, true),
      .now_ms = 12000,
      .dense_bridge = nullptr,
      .dense_lane = {},
      .request_refresh = [](const CorpusChangeSet&) {
        RefreshResult result;
        result.status = RefreshStatus::Busy;
        return result;
      },
  });

  const auto refresh_result = harness.refresh(CorpusChangeSet{});
  assert_true(refresh_result.status == RefreshStatus::Busy,
              "refresh busy integration should preserve the Busy status through the service interface");
}

}  // namespace

int main() {
  try {
    test_failure_degrade_integration_degrades_when_vector_backend_is_unavailable();
    test_failure_degrade_integration_degrades_on_partial_dense_timeout();
    test_failure_degrade_integration_rejects_stale_snapshot_without_query_opt_in();
    test_failure_degrade_integration_returns_refresh_busy();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}