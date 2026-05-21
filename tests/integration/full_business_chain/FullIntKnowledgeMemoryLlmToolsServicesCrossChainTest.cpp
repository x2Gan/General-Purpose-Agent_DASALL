#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for FULLINT-012 memory coverage
#endif

#include "CapabilityServicesLoopbackFixture.h"
#include "AgentFacade.h"
#include "IKnowledgeService.h"
#include "IMemoryManager.h"
#include "ICognitionEngine.h"
#include "IResponseBuilder.h"
#include "KnowledgeErrors.h"
#include "KnowledgeTypes.h"
#include "MockLLMAdapter.h"
#include "MockLLMManager.h"
#include "RuntimeDependencySet.h"
#include "RuntimePolicySnapshot.h"
#include "ToolManager.h"
#include "bridge/ToolServiceBridge.h"
#include "context/ContextPacketGuards.h"
#include "evidence/EvidenceAssembler.h"
#include "execution/BuiltinExecutorLane.h"
#include "facade/KnowledgeService.h"
#include "health/FreshnessController.h"
#include "index/CorpusCatalog.h"
#include "index/IndexReader.h"
#include "prompt/PromptComposeResultGuards.h"
#include "prompt/PromptComposer.h"
#include "query/CorpusRouter.h"
#include "query/QueryNormalizer.h"
#include "registry/ToolRegistry.h"
#include "rerank/Reranker.h"
#include "retrieve/RecallCoordinator.h"
#include "retrieve/SparseRetriever.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::CorpusDescriptor;
using dasall::knowledge::FreshnessController;
using dasall::knowledge::IndexManifest;
using dasall::knowledge::KnowledgeConfigSnapshot;
using dasall::knowledge::KnowledgeErrorCode;
using dasall::knowledge::KnowledgeQuery;
using dasall::knowledge::KnowledgeQueryKind;
using dasall::knowledge::KnowledgeRetrieveResult;
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

constexpr char kRequestId[] = "req-fullint-012-cross-chain";
constexpr char kSessionId[] = "session-fullint-012-cross-chain";
constexpr char kTraceId[] = "trace-fullint-012-cross-chain";
constexpr char kEvidenceCitation[] = "FULLINT-012#cross-chain";

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


[[nodiscard]] bool contains_string(const std::vector<std::string>& values,
                                   const std::string& expected) {
  return std::find(values.begin(), values.end(), expected) != values.end();
}

[[nodiscard]] bool contains_fragment(const std::vector<std::string>& values,
                                     const std::string& expected_fragment) {
  return std::any_of(values.begin(), values.end(),
                     [&expected_fragment](const std::string& value) {
                       return value.find(expected_fragment) != std::string::npos;
                     });
}

[[nodiscard]] bool optional_vector_contains_fragment(
    const std::optional<std::vector<std::string>>& values,
    const std::string& expected_fragment) {
  return values.has_value() && contains_fragment(*values, expected_fragment);
}

[[nodiscard]] bool agent_result_has_tag(
    const dasall::contracts::AgentResult& result,
    const std::string& expected_tag) {
  return result.tags.has_value() && contains_string(*result.tags, expected_tag);
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
          "fullint_012.search");
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
  policy.allowed_corpora = {"fullint-012-corpus"};
  return policy;
}

[[nodiscard]] KnowledgeConfigSnapshot make_knowledge_config() {
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

[[nodiscard]] KnowledgeQuery make_knowledge_query() {
  KnowledgeQuery query;
  query.request_id = kRequestId;
  query.session_id = kSessionId;
  query.query_text = "knowledge memory llm tools services cross chain evidence boundary";
  query.query_kind = KnowledgeQueryKind::PolicyEvidence;
  query.domain_tags = {"normative"};
  query.allowed_corpora = {"fullint-012-corpus"};
  query.top_k = 4U;
  query.max_context_projection_items = 2U;
  return query;
}

[[nodiscard]] CorpusDescriptor make_descriptor() {
  CorpusDescriptor descriptor;
  descriptor.corpus_id = "fullint-012-corpus";
  descriptor.display_name = "FULLINT 012 Corpus";
  descriptor.source_uri = "docs/todos/integration/fullint-012.md";
  descriptor.trust_level = TrustLevel::Trusted;
  descriptor.authority_level = AuthorityLevel::Normative;
  descriptor.source_kind = SourceKind::File;
  descriptor.allowed_formats = {SourceFormat::Markdown};
  descriptor.include_globs = {"*.md"};
  descriptor.exclude_globs = {"archive/*.md"};
  descriptor.supported_modes = {RetrievalMode::LexicalOnly};
  descriptor.active_snapshot_id = "snapshot-fullint-012";
  descriptor.last_updated_ms = 9000;
  descriptor.tags = {"normative"};
  descriptor.metadata = {
      {"baseline_class", "fullint"},
      {"owner_module", "knowledge"},
      {"refresh_strategy", "manual"},
      {"default_language", "en-US"},
  };
  return descriptor;
}

[[nodiscard]] IndexManifest make_manifest() {
  IndexManifest manifest;
  manifest.format_version = 1U;
  manifest.lexical_backend = "sqlite_fts5";
  manifest.tokenizer_profile = "porter unicode61 remove_diacritics 1";
  manifest.snapshot_id = "snapshot-fullint-012";
  manifest.built_at = 8000;
  manifest.effective_at = 9000;
  manifest.document_count = 1U;
  manifest.chunk_count = 1U;
  manifest.vector_enabled = false;
  return manifest;
}

struct KnowledgeEvidenceHarness {
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

  KnowledgeEvidenceHarness()
      : query_normalizer(make_normalize_policy()),
        sparse_retriever(SparseRetrieverDeps{
            .search_index = [this](const SparseIndexSearchRequest& request) {
              return index_reader.search_sparse(request);
            },
        }),
        recall_coordinator(RecallCoordinatorDeps{
                               .sparse_lane = [this](
                                                  const dasall::knowledge::retrieve::SparseRetrieveRequest& request) {
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
        config(make_knowledge_config()) {
    sqlite_index.insert_row(
        "fullint-012-corpus",
        "FULLINT-012",
        "chunk-cross-chain",
        "knowledge memory llm tools services cross chain evidence boundary preserves ContextPacket PromptComposeResult ToolInvocationEnvelope ObservationDigest for FULLINT 012.",
        kEvidenceCitation,
        9500,
        AuthorityLevel::Normative,
        "en-US",
        {"normative"});

    const bool catalog_replaced = corpus_catalog.replace_all({make_descriptor()});
    assert_true(catalog_replaced,
                "FULLINT-012 harness should install a consistent corpus descriptor");

    auto snapshot = std::make_shared<IndexSnapshot>();
    snapshot->manifest = make_manifest();
    snapshot->checksum = "checksum-fullint-012";
    snapshot->search = [this](const SparseIndexSearchRequest& request) {
      return sqlite_index.search(request);
    };

    const bool snapshot_swapped = index_reader.swap_active_snapshot(snapshot);
    assert_true(snapshot_swapped,
                "FULLINT-012 harness should install an active lexical snapshot");

    auto service = std::make_unique<KnowledgeServiceFacade>(make_service_deps());
    assert_true(service->init(config),
                "FULLINT-012 harness should initialize KnowledgeServiceFacade");
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
    deps.assemble_evidence = [this](
                                 const dasall::knowledge::rerank::RankedHitSet& hits,
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

class MemoryDatabaseFixture {
 public:
  explicit MemoryDatabaseFixture(std::string stem)
      : path_(make_temp_database_path(std::move(stem))) {
    cleanup_database_artifacts(path_);
  }

  ~MemoryDatabaseFixture() {
    cleanup_database_artifacts(path_);
  }

  [[nodiscard]] const std::filesystem::path& path() const {
    return path_;
  }

 private:
  [[nodiscard]] static std::filesystem::path make_temp_database_path(std::string stem) {
    const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
    return std::filesystem::temp_directory_path() /
           (std::move(stem) + "-" + std::to_string(timestamp) + ".db");
  }

  static void cleanup_database_artifacts(const std::filesystem::path& database_path) {
    (void)std::filesystem::remove(database_path);
    (void)std::filesystem::remove(database_path.string() + "-wal");
    (void)std::filesystem::remove(database_path.string() + "-shm");
  }

  std::filesystem::path path_;
};

[[nodiscard]] dasall::memory::MemoryConfig make_memory_config(
    const std::filesystem::path& database_path) {
  dasall::memory::MemoryConfig config;
  config.storage.backend = dasall::memory::StorageBackend::Sqlite;
  config.storage.db_path = database_path.string();
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  config.context.compression_trigger_turns = 2;
  config.context.compression_trigger_ratio = 0.5;
  config.vector.enabled = false;
  return config;
}

[[nodiscard]] dasall::memory::MemoryWritebackRequest make_seed_writeback_request() {
  dasall::memory::MemoryWritebackRequest request;
  request.session_id = kSessionId;
  request.turn.turn_id = "turn-fullint-012-seed";
  request.turn.session_id = kSessionId;
  request.turn.user_input = "seed FULLINT-012 cross-chain context";
  request.turn.agent_response = "seed memory response for FULLINT-012";
  request.summary_candidate = dasall::contracts::SummaryMemory{};
  request.summary_candidate->summary_text =
      "FULLINT-012 memory seed summary keeps evidence and service boundaries";
  request.summary_candidate->confirmed_facts = {
      "FULLINT-012 must preserve evidence through context prompt provider and tool services"};

  dasall::memory::FactCandidate fact_candidate;
  fact_candidate.fact.fact_text =
      "FULLINT-012 cross-chain regression requires ObservationDigest after services";
  fact_candidate.fact.fact_type = "integration";
  fact_candidate.fact.confidence_score = 91;
  fact_candidate.fact.source_turn_ids = {"turn-fullint-012-seed"};
  fact_candidate.extraction_source = "turn";
  request.fact_candidates.push_back(std::move(fact_candidate));
  return request;
}

[[nodiscard]] dasall::memory::ContextAssemblyResult assemble_context_from_evidence(
    const KnowledgeRetrieveResult& retrieve_result,
    dasall::memory::IMemoryManager& memory_manager) {
  dasall::memory::MemoryContextRequest request;
  request.request_id = kRequestId;
  request.session_id = kSessionId;
  request.stage = "response";
  request.goal_summary =
      "Verify FULLINT-012 knowledge memory llm tools services cross-chain regression";
  request.constraints_summary =
      "Preserve evidence refs and do not let prompt/tool/service own memory context";
  request.latest_observation_digest_summary =
      "pre-tool observation digest seed: provider accepted context-derived evidence";
  request.visible_tools = {"toggle"};
  request.token_budget_hint = 768;
  request.latency_budget_ms = 250;
  request.external_evidence = retrieve_result.evidence->context_projection;
  request.retrieval_evidence_refs = retrieve_result.retrieval_evidence_refs;
  return memory_manager.prepare_context(request);
}

[[nodiscard]] dasall::contracts::PromptRelease make_prompt_release() {
  return dasall::contracts::PromptRelease{
      .prompt_id = std::string("fullint-012-cross-chain-response"),
      .version = std::string("v1"),
      .stage = dasall::contracts::CompositionStage::Response,
      .eval_status = dasall::contracts::PromptEvalStatus::Stable,
      .release_scope = std::string("integration"),
      .system_instructions =
          std::string("Preserve evidence tags without retrieving new context: {{tags}}"),
      .task_template =
          std::string("Goal={{user_goal}} Constraints={{constraints}} Context={{context_packet_id}} Tools={{visible_tools}} Tags={{tags}}"),
      .output_schema_ref = std::string("schema://fullint-012/response/v1"),
      .few_shot_refs = std::vector<std::string>{
          "inline:assistant: cite FULLINT-012 evidence before invoking services"},
      .policy_notes = std::vector<std::string>{"ADR-006 prompt composer boundary"},
      .rollback_from = std::nullopt,
      .trusted_source = std::string("tests/integration/full_business_chain"),
      .tags = std::vector<std::string>{"fullint-012", "cross-chain"},
  };
}

[[nodiscard]] dasall::contracts::PromptComposeRequest make_prompt_request(
    const dasall::contracts::ContextPacket& context_packet,
    const std::string& source_ref) {
  return dasall::contracts::PromptComposeRequest{
      .request_id = context_packet.request_id,
      .stage = dasall::contracts::CompositionStage::Response,
      .context_packet_id = context_packet.request_id,
      .created_at = 1712746800120LL,
      .task_type = std::string("fullint-012-cross-chain"),
      .prompt_release_id = std::string("fullint-012-cross-chain-response@v1"),
      .visible_tools = std::vector<std::string>{"toggle"},
      .model_route = std::string("loopback.mock"),
      .output_schema_ref = std::string("schema://fullint-012/response/v1"),
      .response_format = std::string("text"),
      .tags = std::vector<std::string>{
          std::string("user_goal=") + context_packet.current_goal_summary.value_or(""),
          std::string("constraints=") + context_packet.policy_digest.value_or(""),
          std::string("source_ref=") + source_ref,
          std::string("evidence_ref=") + kEvidenceCitation,
          std::string("observation_seed=") +
              context_packet.latest_observation_digest_summary.value_or("")},
  };
}

[[nodiscard]] dasall::contracts::PromptComposeResult compose_prompt(
    const dasall::contracts::ContextPacket& context_packet,
    const std::string& source_ref) {
  dasall::llm::prompt::PromptComposer composer;
  assert_true(composer.init(dasall::llm::prompt::PromptComposerConfig{
            .template_engine = "simple_var",
            .max_few_shot_count = 1U,
          }),
              "FULLINT-012 should initialize the real PromptComposer");

  const auto result = composer.compose(make_prompt_request(context_packet, source_ref),
                                       make_prompt_release(),
                                       dasall::llm::prompt::ModelBudgetHint{
                                           .context_window = 4096U,
                                           .max_output_tokens = 256U,
                                           .reserved_output_tokens = 128U,
                                       });

  assert_true(dasall::contracts::validate_prompt_compose_result_field_rules(result).ok,
              "FULLINT-012 PromptComposeResult should satisfy frozen prompt result guards");
  assert_true(result.messages.has_value() && contains_fragment(*result.messages, source_ref),
              "FULLINT-012 prompt messages should preserve context-derived evidence source refs");
  assert_true(result.messages.has_value() && contains_fragment(*result.messages, "toggle"),
              "FULLINT-012 prompt messages should preserve visible tool projection");
  return result;
}

[[nodiscard]] dasall::contracts::LLMRequest make_llm_request(
    const dasall::contracts::PromptComposeResult& compose_result) {
  return dasall::contracts::LLMRequest{
      .request_id = std::string(kRequestId),
      .llm_call_id = std::string("llm-call-fullint-012"),
      .model_route = std::string("loopback.mock"),
      .request_mode = dasall::contracts::LLMRequestMode::Unary,
      .messages = compose_result.messages,
      .created_at = 1712746800130LL,
      .prompt_id = compose_result.selected_prompt_id,
      .prompt_version = compose_result.selected_version,
      .output_schema_ref = std::string("schema://fullint-012/response/v1"),
      .response_format = std::string("text"),
      .runtime_budget = std::nullopt,
      .max_output_tokens = 256U,
      .timeout_ms = 1500U,
      .tags = std::vector<std::string>{"fullint-012", std::string("source_ref=") + kEvidenceCitation},
  };
}

[[nodiscard]] dasall::llm::AdapterCallResult call_provider(
    const dasall::contracts::PromptComposeResult& compose_result,
    dasall::tests::mocks::MockLLMAdapter& adapter) {
  adapter.set_generate_handler([](const dasall::contracts::LLMRequest& request) {
    assert_true(request.messages.has_value() &&
                    contains_fragment(*request.messages, kEvidenceCitation),
                "FULLINT-012 provider handoff should receive evidence-bearing composed messages");
    assert_true(request.prompt_id == std::optional<std::string>{"fullint-012-cross-chain-response"},
                "FULLINT-012 provider handoff should preserve selected prompt id");

    dasall::contracts::LLMResponse response;
    response.request_id = request.request_id;
    response.llm_call_id = request.llm_call_id;
    response.response_kind = dasall::contracts::LLMResponseKind::DirectResponse;
    response.content_payload =
        std::string("provider accepted FULLINT-012 evidence source ") + kEvidenceCitation;
    response.completed_at = 1712746800140LL;
    response.model_name = "loopback-provider";
    response.prompt_id = request.prompt_id;
    response.prompt_version = request.prompt_version;
    response.finish_reason = "stop";
    response.input_tokens = 64U;
    response.output_tokens = 12U;
    response.total_tokens = 76U;
    response.tags = std::vector<std::string>{"fullint-012", "provider-handoff"};

    dasall::llm::AdapterCallResult result;
    result.response = std::move(response);
    return result;
  });

  return adapter.generate(make_llm_request(compose_result));
}

[[nodiscard]] dasall::profiles::RuntimePolicySnapshot make_runtime_policy_snapshot() {
  return dasall::profiles::RuntimePolicySnapshot{
      1U,
      "desktop_full",
      dasall::contracts::RuntimeBudget{
          .max_tokens = 4096U,
          .max_turns = 8U,
          .max_tool_calls = 24U,
          .max_latency_ms = 8000U,
          .max_replan_count = 2U,
      },
      dasall::profiles::ModelProfile{
          .stage_routes = {{
              "response",
              dasall::profiles::ModelRoutePolicy{
                  .route = "loopback.mock",
                  .fallback_route = std::string("builtin_only"),
                  .streaming_enabled = false,
              },
          }},
      },
      dasall::profiles::TokenBudgetPolicy{
          .max_input_tokens = 1024U,
          .max_output_tokens = 512U,
          .max_history_turns = 4U,
          .compression_threshold = 768U,
      },
      dasall::profiles::PromptPolicy{
          .allowed_prompt_releases = {"stable"},
          .trusted_sources = {"tests"},
          .tool_visibility_rules = {"builtin:toggle"},
      },
      dasall::profiles::CapabilityCachePolicy{
          .refresh_interval_ms = 10000,
          .expire_after_ms = 180000,
          .stale_read_allowed = false,
          .failure_backoff_ms = 5000,
      },
      dasall::profiles::DegradePolicy{
          .fallback_chain = {"builtin_only"},
          .allow_model_failover = false,
          .allow_budget_degrade = true,
      },
      dasall::profiles::TimeoutPolicy{
          .llm = dasall::profiles::TimeoutBudget{
              .timeout_ms = 1800,
              .retry_budget = 0U,
              .circuit_breaker_threshold = 3U,
          },
          .tool = dasall::profiles::TimeoutBudget{
              .timeout_ms = 2500,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 4U,
          },
          .mcp = dasall::profiles::TimeoutBudget{
              .timeout_ms = 2000,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 3U,
          },
          .workflow = dasall::profiles::TimeoutBudget{
              .timeout_ms = 5000,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 3U,
          },
      },
      dasall::profiles::ExecutionPolicy{
          .requires_high_risk_confirmation = true,
          .safe_mode_enabled = true,
          .audit_level = "full",
          .allowed_tool_domains = {"builtin"},
      },
      dasall::profiles::OpsPolicy{
          .log_level = "info",
          .metrics_granularity = "full",
          .trace_sample_ratio = 0.5,
          .remote_diagnostics_enabled = false,
          .upgrade_strategy = "rolling",
      },
      4U};
}

    [[nodiscard]] std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot>
    make_runtime_full_chain_policy_snapshot() {
      return std::make_shared<const dasall::profiles::RuntimePolicySnapshot>(
        1U,
        "desktop_full",
        dasall::contracts::RuntimeBudget{
          .max_tokens = 4096U,
          .max_turns = 8U,
          .max_tool_calls = 24U,
          .max_latency_ms = 8000U,
          .max_replan_count = 2U,
        },
        dasall::profiles::ModelProfile{
          .stage_routes = {
              {"main",
               dasall::profiles::ModelRoutePolicy{
                 .route = "loopback.mock",
                 .fallback_route = std::string("builtin_only"),
                 .streaming_enabled = false,
               }},
            {"planning",
             dasall::profiles::ModelRoutePolicy{
               .route = "loopback.mock",
               .fallback_route = std::string("builtin_only"),
               .streaming_enabled = false,
             }},
            {"execution",
             dasall::profiles::ModelRoutePolicy{
               .route = "loopback.mock",
               .fallback_route = std::string("builtin_only"),
               .streaming_enabled = false,
             }},
            {"reflection",
             dasall::profiles::ModelRoutePolicy{
               .route = "loopback.mock",
               .fallback_route = std::string("builtin_only"),
               .streaming_enabled = false,
             }},
            {"response",
             dasall::profiles::ModelRoutePolicy{
               .route = "loopback.mock",
               .fallback_route = std::string("builtin_only"),
               .streaming_enabled = false,
             }},
          },
        },
        dasall::profiles::TokenBudgetPolicy{
          .max_input_tokens = 1024U,
          .max_output_tokens = 512U,
          .max_history_turns = 4U,
          .compression_threshold = 768U,
        },
        dasall::profiles::PromptPolicy{
          .allowed_prompt_releases = {"stable"},
          .trusted_sources = {"tests"},
          .tool_visibility_rules = {"builtin:agent.dataset"},
        },
        dasall::profiles::CapabilityCachePolicy{
          .refresh_interval_ms = 10000,
          .expire_after_ms = 180000,
          .stale_read_allowed = false,
          .failure_backoff_ms = 5000,
        },
        dasall::profiles::DegradePolicy{
          .fallback_chain = {"builtin_only"},
          .allow_model_failover = false,
          .allow_budget_degrade = true,
        },
        dasall::profiles::TimeoutPolicy{
          .llm = dasall::profiles::TimeoutBudget{
            .timeout_ms = 1800,
            .retry_budget = 0U,
            .circuit_breaker_threshold = 3U,
          },
          .tool = dasall::profiles::TimeoutBudget{
            .timeout_ms = 2500,
            .retry_budget = 1U,
            .circuit_breaker_threshold = 4U,
          },
          .mcp = dasall::profiles::TimeoutBudget{
            .timeout_ms = 2000,
            .retry_budget = 1U,
            .circuit_breaker_threshold = 3U,
          },
          .workflow = dasall::profiles::TimeoutBudget{
            .timeout_ms = 5000,
            .retry_budget = 1U,
            .circuit_breaker_threshold = 3U,
          },
        },
        dasall::profiles::ExecutionPolicy{
          .requires_high_risk_confirmation = true,
          .safe_mode_enabled = true,
          .audit_level = "full",
          .allowed_tool_domains = {"builtin"},
        },
        dasall::profiles::OpsPolicy{
          .log_level = "info",
          .metrics_granularity = "full",
          .trace_sample_ratio = 0.5,
          .remote_diagnostics_enabled = false,
          .upgrade_strategy = "rolling",
        },
        4U);
    }

    [[nodiscard]] std::string make_runtime_query_planning_payload() {
      return std::string{"{"}
         + "\"schema_version\":\"cognition.plan.v1\"," 
         + "\"plan_id\":\"plan-fullint-012-runtime\"," 
         + "\"revision\":1,"
         + "\"nodes\":[{"
         + "\"node_id\":\"plan-node:fullint-012-runtime\"," 
         + "\"objective\":\"collect cross-chain evidence through agent.dataset\"," 
         + "\"success_signal\":\"fullint_runtime_query_complete\"," 
         + "\"action_kind_hint\":\"tool_action\"," 
         + "\"depends_on\":[],"
         + "\"evidence_refs\":[\"" + std::string{kEvidenceCitation} + "\"]}],"
         + "\"edges\":[],"
         + "\"open_questions\":[],"
         + "\"plan_rationale\":\"runtime full chain should keep knowledge evidence while routing through services data lane\"," 
         + "\"estimated_complexity\":1}"
         ;
    }

    [[nodiscard]] std::string make_runtime_query_execution_payload() {
      return std::string{"{"}
         + "\"schema_version\":\"cognition.reasoning.v1\"," 
         + "\"decision_kind\":\"ExecuteAction\"," 
         + "\"confidence\":0.82,"
         + "\"rationale\":\"fullint runtime path should use agent.dataset instead of the direct llm marker\"," 
         + "\"selected_node_id\":\"plan-node:fullint-012-runtime\"," 
         + "\"tool_intent_hint\":{"
         + "\"tool_name\":\"agent.dataset\"," 
         + "\"intent_summary\":\"query services data lane for runtime path evidence\"," 
         + "\"argument_hints\":[\"status\"],"
         + "\"evidence_refs\":[\"" + std::string{kEvidenceCitation} + "\"]},"
         + "\"clarification_needed\":false,"
         + "\"clarification_question\":null,"
         + "\"response_outline\":{"
         + "\"summary\":\"runtime full chain response\"," 
         + "\"key_points\":[\"preserve knowledge evidence identity\",\"avoid direct llm path marker drift\"]},"
         + "\"candidate_scores\":[{"
         + "\"candidate_name\":\"execute_action\"," 
         + "\"score\":0.82,"
         + "\"rationale\":\"runtime full chain should execute the builtin query tool\"}]}"
         ;
    }

[[nodiscard]] dasall::contracts::ToolDescriptor make_toggle_descriptor() {
  return dasall::contracts::ToolDescriptor{
      .tool_name = std::string("toggle"),
      .display_name = std::string("Loopback Toggle"),
      .category = dasall::contracts::ToolCategory::Action,
      .capability_tier = dasall::contracts::ToolCapabilityTier::Preview,
      .is_read_only = false,
      .supports_compensation = false,
      .default_timeout_ms = 2500U,
      .input_schema_ref = std::string("schema://tools/toggle/input/v1"),
      .output_schema_ref = std::string("schema://tools/toggle/output/v1"),
      .required_scopes = std::vector<std::string>{"tools.execute"},
      .tags = std::vector<std::string>{"builtin", "fullint-012"},
      .version = std::string("1.0.0"),
  };
}

[[nodiscard]] dasall::tools::ToolManager make_tool_manager(
    dasall::tests::mocks::CapabilityServicesLoopbackFixture& services_fixture) {
  auto registry = std::make_shared<dasall::tools::registry::ToolRegistry>();
  assert_true(registry->register_builtin(make_toggle_descriptor()),
              "FULLINT-012 should register a test-local toggle descriptor");

  auto execution_service = std::shared_ptr<dasall::services::IExecutionService>(
      &services_fixture.execution_service(), [](dasall::services::IExecutionService*) {});
  auto data_service = std::shared_ptr<dasall::services::IDataService>(
      &services_fixture.data_service(), [](dasall::services::IDataService*) {});

  auto builtin_lane = std::make_shared<dasall::tools::execution::BuiltinExecutorLane>(
      dasall::tools::execution::BuiltinExecutorLaneDependencies{
          .registry = registry,
          .service_bridge = nullptr,
          .execution_service = std::move(execution_service),
          .data_service = std::move(data_service),
          .now_ms = [] { return 1712746800150LL; },
      });

  dasall::tools::manager::ToolManagerDependencies dependencies;
  dependencies.registry = registry;
  dependencies.executor = [builtin_lane](const auto& execution_request) {
    return builtin_lane->execute(
        execution_request.tool_ir,
        dasall::tools::ToolExecutionContext{
            .invocation_context = execution_request.invocation_context,
            .lane_key = execution_request.route_decision.lane_key,
        });
  };
  return dasall::tools::ToolManager(std::move(dependencies));
}

[[nodiscard]] dasall::contracts::ToolDescriptor make_dataset_descriptor() {
  return dasall::contracts::ToolDescriptor{
      .tool_name = std::string("agent.dataset"),
      .display_name = std::string("Agent Dataset"),
      .category = dasall::contracts::ToolCategory::Information,
      .capability_tier = dasall::contracts::ToolCapabilityTier::Preview,
      .is_read_only = true,
      .supports_compensation = false,
      .default_timeout_ms = 2500U,
      .input_schema_ref = std::string("schema://tools/agent.dataset/input/v1"),
      .output_schema_ref = std::string("schema://tools/agent.dataset/output/v1"),
      .required_scopes = std::vector<std::string>{"tools.read"},
      .tags = std::vector<std::string>{"builtin", "fullint-012", "query"},
      .version = std::string("1.0.0"),
  };
}

[[nodiscard]] dasall::tools::ToolManager make_runtime_query_tool_manager(
    dasall::tests::mocks::CapabilityServicesLoopbackFixture& services_fixture) {
  auto registry = std::make_shared<dasall::tools::registry::ToolRegistry>();
  assert_true(registry->register_builtin(make_dataset_descriptor()),
              "FULLINT-012 runtime path should register a query builtin descriptor");

  auto data_service = std::shared_ptr<dasall::services::IDataService>(
      &services_fixture.data_service(), [](dasall::services::IDataService*) {});

  auto builtin_lane = std::make_shared<dasall::tools::execution::BuiltinExecutorLane>(
      dasall::tools::execution::BuiltinExecutorLaneDependencies{
          .registry = registry,
          .service_bridge = std::make_shared<dasall::tools::bridge::ToolServiceBridge>(),
          .execution_service = nullptr,
          .data_service = std::move(data_service),
          .now_ms = [] { return 1712746800190LL; },
      });

  dasall::tools::manager::ToolManagerDependencies dependencies;
  dependencies.registry = registry;
  dependencies.executor = [builtin_lane](const auto& execution_request) {
    return builtin_lane->execute(
        execution_request.tool_ir,
        dasall::tools::ToolExecutionContext{
            .invocation_context = execution_request.invocation_context,
            .lane_key = execution_request.route_decision.lane_key,
        });
  };
  return dasall::tools::ToolManager(std::move(dependencies));
}

[[nodiscard]] dasall::contracts::ToolRequest make_toggle_request(
    const dasall::contracts::LLMResponse& provider_response) {
  return dasall::contracts::ToolRequest{
      .request_id = std::string(kRequestId) + ".tool",
      .tool_call_id = std::string("tool-call-fullint-012-toggle"),
      .tool_name = std::string("toggle"),
      .invocation_kind = dasall::contracts::ToolInvocationKind::Action,
      .arguments_payload = std::string("{\"state\":\"on\",\"evidence\":\"") +
                           kEvidenceCitation + "\",\"provider\":\"" +
                           provider_response.model_name.value_or("unknown") + "\"}",
      .created_at = 1712746800160LL,
      .goal_id = std::string("goal-fullint-012"),
      .worker_task_id = std::string("worker-fullint-012"),
      .runtime_budget = std::nullopt,
      .timeout_ms = 2500U,
      .idempotency_key = std::string("idem-fullint-012-toggle"),
      .tags = std::vector<std::string>{"fullint-012", std::string("source_ref=") + kEvidenceCitation},
  };
}

[[nodiscard]] dasall::tools::ToolInvocationContext make_tool_context(
    const dasall::profiles::RuntimePolicySnapshot& snapshot) {
  return dasall::tools::ToolInvocationContext{
      .caller_domain = std::string("runtime.fullint-012"),
      .session_id = std::string(kSessionId),
      .profile_snapshot = &snapshot,
      .trace = {
          .trace_id = std::string(kTraceId),
          .span_id = std::string("span-fullint-012-tool"),
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::vector<dasall::tools::ToolConfirmationFact>{
          dasall::tools::ToolConfirmationFact{
              .confirmation_id = std::string("confirm-fullint-012"),
              .subject_ref = std::string("goal://fullint-012"),
              .proof_type = std::string("integration.approved"),
              .confirmed_at_ms = 1712746800155LL,
          }},
  };
}

void assert_tool_envelope_preserves_cross_chain_facts(
    const dasall::tools::ToolInvocationEnvelope& envelope) {
  assert_true(envelope.tool_result.has_value() && envelope.tool_result->success.value_or(false),
              "FULLINT-012 tool invocation should succeed through ToolManager and services");
  assert_true(envelope.observation.has_value() && envelope.observation_digest.has_value(),
              "FULLINT-012 tool invocation should produce Observation and ObservationDigest");
  assert_true(envelope.route_facts.has_value() && envelope.evidence_refs.has_value(),
              "FULLINT-012 tool invocation should expose route facts and evidence refs");
  assert_true(envelope.tool_result->payload.has_value() &&
                  envelope.tool_result->payload->find("\"operation\":\"toggle\"") !=
                      std::string::npos,
              "FULLINT-012 service payload should come from the loopback execution lane");
  assert_true(envelope.observation_digest->citations.has_value() &&
                  contains_string(*envelope.observation_digest->citations,
                                  std::string("tool_call:") +
                                      envelope.tool_result->tool_call_id.value_or("")) &&
                  contains_string(*envelope.observation_digest->citations, "route_kind:builtin"),
              "FULLINT-012 ObservationDigest should keep tool call and route citations");
  assert_true(optional_vector_contains_fragment(envelope.observation_digest->tags, "action"),
              "FULLINT-012 ObservationDigest should preserve executor projection tags");
  assert_true(envelope.observation_digest->summary.has_value() &&
                  !envelope.observation_digest->summary->empty() &&
                  envelope.observation_digest->key_facts.has_value() &&
                  !envelope.observation_digest->key_facts->empty(),
              "FULLINT-012 ObservationDigest should not be empty after services projection");
}

void test_cross_chain_preserves_evidence_through_provider_and_services() {
  KnowledgeEvidenceHarness knowledge_harness;
  const auto retrieve_result = knowledge_harness.knowledge_service->retrieve(make_knowledge_query());
  assert_true(retrieve_result.ok,
              "FULLINT-012 knowledge retrieve should succeed through KnowledgeServiceFacade");
  assert_true(retrieve_result.has_consistent_values(),
              "FULLINT-012 knowledge result should keep consistent evidence shape");
  assert_true(retrieve_result.evidence.has_value() &&
                  !retrieve_result.evidence->context_projection.empty(),
              "FULLINT-012 knowledge result should return context-projection evidence");
  assert_true(!retrieve_result.retrieval_evidence_refs.empty() &&
                  retrieve_result.retrieval_evidence_refs.front().source_ref == kEvidenceCitation,
              "FULLINT-012 knowledge result should preserve structured evidence source refs");

  MemoryDatabaseFixture memory_database("dasall-fullint-012-memory");
  const auto memory_config = make_memory_config(memory_database.path());
  auto memory_manager = dasall::memory::create_memory_manager(memory_config);
  assert_true(static_cast<int>(memory_manager->init(memory_config)) == 0,
              "FULLINT-012 memory manager should initialize with sqlite backend");
  const auto writeback_result = memory_manager->write_back(make_seed_writeback_request());
  assert_true(!writeback_result.result_code.has_value(),
              "FULLINT-012 memory seed writeback should succeed before context assembly");

  const auto context_result = assemble_context_from_evidence(retrieve_result, *memory_manager);
  assert_true(!context_result.result_code.has_value() && !context_result.degraded,
              "FULLINT-012 memory context assembly should remain successful and non-degraded");
  assert_true(dasall::contracts::validate_context_packet_field_rules(
                  context_result.context_packet)
                  .ok,
              "FULLINT-012 ContextPacket should satisfy frozen context guards");
  assert_true(optional_vector_contains_fragment(
                  context_result.context_packet.retrieval_evidence, kEvidenceCitation),
              "FULLINT-012 ContextPacket should preserve citation-bearing evidence text");
  assert_true(context_result.context_packet.retrieval_evidence_refs.has_value() &&
                  !context_result.context_packet.retrieval_evidence_refs->empty() &&
                  context_result.context_packet.retrieval_evidence_refs->front().source_ref ==
                      kEvidenceCitation,
              "FULLINT-012 ContextPacket should preserve structured RetrievalEvidenceRef");
  assert_true(context_result.context_packet.active_tools.has_value() &&
                  contains_string(*context_result.context_packet.active_tools, "toggle"),
              "FULLINT-012 ContextPacket should preserve active tool visibility");

  const auto compose_result =
      compose_prompt(context_result.context_packet,
                     context_result.context_packet.retrieval_evidence_refs->front().source_ref);
  dasall::tests::mocks::MockLLMAdapter adapter;
  const auto provider_result = call_provider(compose_result, adapter);
  assert_true(adapter.generate_call_count() == 1,
              "FULLINT-012 provider adapter should be called exactly once on the happy path");
  assert_true(provider_result.response.has_value() &&
                  provider_result.response->content_payload.has_value() &&
                  provider_result.response->content_payload->find(kEvidenceCitation) !=
                      std::string::npos,
              "FULLINT-012 provider response should preserve evidence identity");

  dasall::tests::mocks::CapabilityServicesLoopbackFixtureOptions service_options;
  service_options.profile_id = "desktop_full";
  service_options.execution_capability_id = "toggle";
  service_options.data_capability_id = "dataset";
  service_options.now_ms = 1712746800170ULL;
  dasall::tests::mocks::CapabilityServicesLoopbackFixture services_fixture(service_options);
  auto tool_manager = make_tool_manager(services_fixture);
  const auto snapshot = make_runtime_policy_snapshot();
  const auto envelope = tool_manager.invoke(make_toggle_request(*provider_result.response),
                                            make_tool_context(snapshot));
  assert_tool_envelope_preserves_cross_chain_facts(envelope);
  assert_true(!services_fixture.local_requests().empty() &&
                  services_fixture.local_requests().front().operation_name == "toggle",
              "FULLINT-012 services fixture should receive the real loopback action request");
  assert_true(!services_fixture.local_requests().empty() &&
                  services_fixture.local_requests().front().payload_json.find(kEvidenceCitation) !=
                      std::string::npos,
              "FULLINT-012 services fixture should receive the evidence-bearing tool payload");

  memory_manager->shutdown();
}

  void test_cross_chain_runtime_path_stays_tool_positive() {
    KnowledgeEvidenceHarness knowledge_harness;

    MemoryDatabaseFixture memory_database("dasall-fullint-012-runtime-memory");
    const auto memory_config = make_memory_config(memory_database.path());
    std::shared_ptr<dasall::memory::IMemoryManager> memory_manager(
      dasall::memory::create_memory_manager(memory_config));
    assert_true(static_cast<int>(memory_manager->init(memory_config)) == 0,
          "FULLINT-012 runtime path memory manager should initialize with sqlite backend");
    const auto writeback_result = memory_manager->write_back(make_seed_writeback_request());
    assert_true(!writeback_result.result_code.has_value(),
          "FULLINT-012 runtime path memory seed writeback should succeed before runtime assembly");

    auto llm_manager = std::make_shared<dasall::tests::mocks::MockLLMManager>();
    llm_manager->set_stage_result(
      "planning",
      dasall::tests::mocks::MockLLMManager::make_structured_stage_result(
        "planning",
        make_runtime_query_planning_payload(),
        std::string{kRequestId}));
    llm_manager->set_stage_result(
      "execution",
      dasall::tests::mocks::MockLLMManager::make_structured_stage_result(
        "execution",
        make_runtime_query_execution_payload(),
        std::string{kRequestId}));
    llm_manager->set_stage_result(
      "response",
      dasall::tests::mocks::MockLLMManager::make_success_result(
        std::string("FULLINT-012 runtime response ") + kEvidenceCitation,
        "loopback.mock",
        std::string{kRequestId}));

    dasall::tests::mocks::CapabilityServicesLoopbackFixtureOptions service_options;
    service_options.profile_id = "desktop_full";
    service_options.execution_capability_id = "toggle";
    service_options.data_capability_id = "agent.dataset";
    service_options.now_ms = 1712746800200ULL;
    dasall::tests::mocks::CapabilityServicesLoopbackFixture services_fixture(service_options);

    auto dependency_set = std::make_shared<dasall::runtime::RuntimeDependencySet>();
    dependency_set->memory_manager = memory_manager;
      dependency_set->cognition_engine = std::shared_ptr<dasall::cognition::ICognitionEngine>(
        dasall::cognition::create_cognition_engine());
      dependency_set->response_builder = std::shared_ptr<dasall::cognition::IResponseBuilder>(
        dasall::cognition::create_response_builder());
    dependency_set->knowledge_service = std::shared_ptr<dasall::knowledge::IKnowledgeService>(
      knowledge_harness.knowledge_service.get(),
      [](dasall::knowledge::IKnowledgeService*) {});
    dependency_set->llm_manager = llm_manager;
    dependency_set->tool_manager = std::make_shared<dasall::tools::ToolManager>(
      make_runtime_query_tool_manager(services_fixture));
      dependency_set->local_stub_ports.main_loop_exit =
        dasall::runtime::RuntimeStubMainLoopExit::ToolRound;
    dependency_set->visible_tools = {"agent.dataset"};
    dependency_set->external_evidence = {"runtime:fullint-012:tool-positive"};

    dasall::runtime::AgentInitRequest init_request;
    init_request.runtime_instance_id = "rt-fullint-012-runtime-path";
    init_request.profile_id = "desktop_full";
    init_request.policy_snapshot = make_runtime_full_chain_policy_snapshot();
    init_request.dependency_set = dependency_set;
    init_request.boot_reason = "fullint-012-runtime-path";
    init_request.cold_start = true;

    dasall::runtime::AgentFacade facade;
    const auto init_result = facade.init(init_request);
    assert_true(init_result.accepted,
          "FULLINT-012 runtime path integration should initialize AgentFacade with full-chain ports");

    dasall::contracts::AgentRequest request;
    request.request_id = std::string{kRequestId};
    request.session_id = std::string{kSessionId};
    request.trace_id = std::string{kTraceId};
    request.user_input = std::string("runtime fullint cross-chain query");
    request.request_channel = dasall::contracts::RequestChannel::Cli;
    request.created_at = 1712746800210LL;

    const auto result = facade.handle(request);
    const auto status_name = [&result]() {
      if (!result.status.has_value()) {
        return std::string{"<unset>"};
      }

      switch (*result.status) {
        case dasall::contracts::AgentResultStatus::Unspecified:
          return std::string{"Unspecified"};
        case dasall::contracts::AgentResultStatus::Completed:
          return std::string{"Completed"};
        case dasall::contracts::AgentResultStatus::Failed:
          return std::string{"Failed"};
        case dasall::contracts::AgentResultStatus::PartiallyCompleted:
          return std::string{"PartiallyCompleted"};
        case dasall::contracts::AgentResultStatus::Cancelled:
          return std::string{"Cancelled"};
        case dasall::contracts::AgentResultStatus::Timeout:
          return std::string{"Timeout"};
      }

      return std::string{"<unknown>"};
    };
    const auto error_message = [&result]() {
      if (!result.error_info.has_value()) {
        return std::string{"<none>"};
      }

      return result.error_info->details.message.empty()
                 ? std::string{"<empty>"}
                 : result.error_info->details.message;
    };
    assert_true(result.status == dasall::contracts::AgentResultStatus::Completed,
                "FULLINT-012 runtime path integration should complete successfully; status=" +
                    status_name() + " response=" +
                    result.response_text.value_or(std::string{"<none>"}) + " error=" +
                    error_message());
    assert_true(agent_result_has_tag(result, "runtime_path:tool_positive"),
          "FULLINT-012 runtime path integration should classify the full chain as tool_positive");
    assert_true(!agent_result_has_tag(result, "runtime_path:direct_llm"),
          "FULLINT-012 runtime path integration should not drift back to the direct_llm tag");
    assert_true(!agent_result_has_tag(result, "runtime_path:cognition_first"),
          "FULLINT-012 runtime path integration should not mix cognition_first into the tool-positive case");
    assert_true(!agent_result_has_tag(result, "runtime_path:recovery_positive"),
          "FULLINT-012 runtime path integration should not claim recovery_positive on the happy tool path");
    assert_true(!services_fixture.local_requests().empty() &&
            services_fixture.local_requests().front().capability_id == "agent.dataset",
          "FULLINT-012 runtime path integration should route through the services data lane capability id");
    assert_true(!services_fixture.local_requests().empty() &&
              services_fixture.local_requests().front().operation_name == "default",
          "FULLINT-012 runtime path integration should preserve the query projection on the loopback request");
    assert_true(result.response_text.has_value() && !result.response_text->empty(),
                "FULLINT-012 runtime path integration should still produce a final response");

    memory_manager->shutdown();
  }

void test_cross_chain_fail_closed_without_evidence_or_descriptor() {
  KnowledgeEvidenceHarness stale_knowledge_harness;
  stale_knowledge_harness.now_ms = 70000;
  const auto stale_result = stale_knowledge_harness.knowledge_service->retrieve(make_knowledge_query());
  assert_true(!stale_result.ok,
              "FULLINT-012 stale knowledge snapshot should be rejected before context assembly");
  assert_true(stale_result.error.has_value(),
              "FULLINT-012 stale knowledge rejection should carry an explicit error");
  assert_equal(static_cast<int>(KnowledgeErrorCode::IndexStaleRejected),
               stale_result.error->details.code.value_or(-1),
               "FULLINT-012 stale knowledge rejection should use IndexStaleRejected");
  assert_true(!stale_result.evidence.has_value() &&
                  stale_result.retrieval_evidence_refs.empty(),
              "FULLINT-012 stale knowledge rejection should not leak evidence");

  auto empty_registry = std::make_shared<dasall::tools::registry::ToolRegistry>(
      std::vector<dasall::contracts::ToolDescriptor>{});
  auto builtin_lane = std::make_shared<dasall::tools::execution::BuiltinExecutorLane>(
      dasall::tools::execution::BuiltinExecutorLaneDependencies{
          .registry = empty_registry,
          .service_bridge = nullptr,
          .execution_service = nullptr,
          .data_service = nullptr,
          .now_ms = [] { return 1712746800180LL; },
      });
  dasall::tools::manager::ToolManagerDependencies dependencies;
  dependencies.registry = empty_registry;
  dependencies.executor = [builtin_lane](const auto& execution_request) {
    return builtin_lane->execute(
        execution_request.tool_ir,
        dasall::tools::ToolExecutionContext{
            .invocation_context = execution_request.invocation_context,
            .lane_key = execution_request.route_decision.lane_key,
        });
  };
  dasall::tools::ToolManager manager(std::move(dependencies));
  const auto snapshot = make_runtime_policy_snapshot();
  const auto missing_envelope = manager.invoke(
      dasall::contracts::ToolRequest{
          .request_id = std::string("req-fullint-012-missing-tool"),
          .tool_call_id = std::string("tool-call-fullint-012-missing"),
          .tool_name = std::string("missing.fullint.012"),
          .invocation_kind = dasall::contracts::ToolInvocationKind::Action,
          .arguments_payload = std::string("{}"),
          .created_at = 1712746800185LL,
          .goal_id = std::string("goal-fullint-012"),
          .worker_task_id = std::string("worker-fullint-012"),
          .runtime_budget = std::nullopt,
          .timeout_ms = 2500U,
          .idempotency_key = std::string("idem-fullint-012-missing"),
          .tags = std::vector<std::string>{"fullint-012", "negative"},
      },
      make_tool_context(snapshot));

  assert_true(missing_envelope.tool_result.has_value() &&
                  !missing_envelope.tool_result->success.value_or(true),
              "FULLINT-012 missing descriptor should fail closed with ToolResult failure");
  assert_true(!missing_envelope.has_projection(),
              "FULLINT-012 missing descriptor should not fabricate ObservationDigest");
  assert_true(missing_envelope.failure_reason_code.has_value() &&
                  missing_envelope.failure_reason_code->find("descriptor_missing") !=
                      std::string::npos,
              "FULLINT-012 missing descriptor should expose descriptor_missing reason");
}

}  // namespace

int main() {
  try {
    test_cross_chain_preserves_evidence_through_provider_and_services();
    test_cross_chain_runtime_path_stays_tool_positive();
    test_cross_chain_fail_closed_without_evidence_or_descriptor();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
