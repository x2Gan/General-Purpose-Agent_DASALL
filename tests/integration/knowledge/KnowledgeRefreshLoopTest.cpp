#include <chrono>
#include <condition_variable>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "IKnowledgeService.h"
#include "KnowledgeTypes.h"
#include "evidence/EvidenceAssembler.h"
#include "facade/KnowledgeService.h"
#include "health/FreshnessController.h"
#include "index/CorpusCatalog.h"
#include "index/IndexReader.h"
#include "index/IndexWriter.h"
#include "index/VersionLedger.h"
#include "ingest/IngestionCoordinator.h"
#include "ingest/SourceScanner.h"
#include "query/CorpusRouter.h"
#include "query/QueryNormalizer.h"
#include "rerank/Reranker.h"
#include "retrieve/RecallCoordinator.h"
#include "retrieve/SparseRetriever.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::CorpusChangeSet;
using dasall::knowledge::CorpusDescriptor;
using dasall::knowledge::EvidenceBundle;
using dasall::knowledge::IndexManifest;
using dasall::knowledge::KnowledgeConfigSnapshot;
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
using dasall::knowledge::index::IndexWriter;
using dasall::knowledge::index::IndexWriterDeps;
using dasall::knowledge::index::VersionLedger;
using dasall::knowledge::index::VersionLedgerDeps;
using dasall::knowledge::index::VersionLedgerEntry;
using dasall::knowledge::ingest::ChunkPolicy;
using dasall::knowledge::ingest::ChunkStrategy;
using dasall::knowledge::ingest::CorpusScanPlan;
using dasall::knowledge::ingest::IngestionCoordinator;
using dasall::knowledge::ingest::IngestionCoordinatorDeps;
using dasall::knowledge::ingest::SourceRecord;
using dasall::knowledge::ingest::SourceScanner;
using dasall::knowledge::ingest::SourceScannerDeps;
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
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

constexpr std::string_view kCorpusId = "adr_normative";
constexpr std::string_view kDocumentSourceUri = "docs/adr/ADR-REFRESH.md";
constexpr std::string_view kBaselineToken = "refreshstableanchor";
constexpr std::string_view kUpdatedToken = "refreshupdatedanchor";

class TempDirectory {
 public:
  explicit TempDirectory(std::string name)
      : path_(std::filesystem::temp_directory_path() / std::move(name)) {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
    std::filesystem::create_directories(path_);
  }

  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  [[nodiscard]] const std::filesystem::path& path() const {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

void write_file(const std::filesystem::path& path, std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << content;
}

[[nodiscard]] QueryNormalizePolicy make_normalize_policy() {
  QueryNormalizePolicy policy;
  policy.max_query_text_bytes = 512U;
  policy.max_lexical_terms = 16U;
  policy.max_top_k = 8U;
  policy.max_context_projection_items = 4U;
  policy.allowed_corpora = {std::string(kCorpusId)};
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

[[nodiscard]] ChunkPolicy make_chunk_policy() {
  ChunkPolicy policy;
  policy.strategy = ChunkStrategy::FixedSize;
  policy.target_chunk_chars = 128U;
  policy.max_chunk_chars = 192U;
  policy.overlap_chars = 0U;
  policy.min_chunk_chars = 24U;
  return policy;
}

[[nodiscard]] CorpusDescriptor make_descriptor() {
  CorpusDescriptor descriptor;
  descriptor.corpus_id = std::string(kCorpusId);
  descriptor.display_name = "ADR Normative";
  descriptor.source_uri = "docs/adr";
  descriptor.trust_level = TrustLevel::Trusted;
  descriptor.authority_level = AuthorityLevel::Normative;
  descriptor.source_kind = SourceKind::File;
  descriptor.allowed_formats = {SourceFormat::Markdown};
  descriptor.include_globs = {"ADR-*.md"};
  descriptor.exclude_globs = {};
  descriptor.supported_modes = {RetrievalMode::LexicalOnly};
  descriptor.active_snapshot_id = "snapshot-bootstrap";
  descriptor.last_updated_ms = 1713657600000LL;
  descriptor.tags = {"knowledge"};
  descriptor.metadata = {
      {"baseline_class", "knowledge"},
      {"owner_module", "knowledge"},
      {"refresh_strategy", "manual"},
      {"default_language", "zh-CN"},
  };
  return descriptor;
}

[[nodiscard]] KnowledgeQuery make_query(std::string request_id, std::string query_text) {
  KnowledgeQuery query;
  query.request_id = std::move(request_id);
  query.query_text = std::move(query_text);
  query.query_kind = KnowledgeQueryKind::PolicyEvidence;
  query.allowed_corpora = {std::string(kCorpusId)};
  query.top_k = 4U;
  query.max_context_projection_items = 2U;
  return query;
}

[[nodiscard]] SparseIndexSearchRequest make_search_request(std::string term) {
  SparseIndexSearchRequest request;
  request.expression.match_expression = '"' + term + '"';
  request.expression.lexical_terms = {std::move(term)};
  request.allowed_corpus_ids = {std::string(kCorpusId)};
  request.minimum_authority_level = AuthorityLevel::Reference;
  request.top_k = 4U;
  return request;
}

[[nodiscard]] bool evidence_contains_token(const EvidenceBundle& evidence, std::string_view token) {
  for (const auto& slice : evidence.slices) {
    if (slice.snippet.find(token) != std::string::npos ||
        slice.citation_ref.find(token) != std::string::npos) {
      return true;
    }
  }

  for (const auto& projection : evidence.context_projection) {
    if (projection.find(token) != std::string::npos) {
      return true;
    }
  }

  return false;
}

class KnowledgeRefreshLoopHarness {
 public:
  KnowledgeRefreshLoopHarness()
      : temp_directory_("dasall-knowledge-refresh-loop-test"),
        source_path_(temp_directory_.path() / kDocumentSourceUri),
        config_(make_config()),
        descriptor_(make_descriptor()) {
    auto corpus_catalog = std::make_unique<CorpusCatalog>();
    assert_true(corpus_catalog->replace_all({descriptor_}),
                "refresh loop harness should install a consistent corpus descriptor");
    corpus_catalog_ptr_ = corpus_catalog.get();

    auto index_reader = std::make_unique<IndexReader>();
    index_reader_ptr_ = index_reader.get();

    ledger_ = std::make_unique<VersionLedger>(VersionLedgerDeps{
        .read_snapshot_checksum = [this](std::string_view snapshot_id) {
          return index_reader_ptr_->read_snapshot_checksum(snapshot_id);
        },
    });

    sparse_retriever_ = std::make_unique<SparseRetriever>(SparseRetrieverDeps{
        .search_index = [this](const SparseIndexSearchRequest& request) {
          return index_reader_ptr_->search_sparse(request);
        },
    });

    IngestionCoordinatorDeps ingestion_deps;
    ingestion_deps.load_catalog_snapshot = [this]() {
      return corpus_catalog_ptr_->snapshot();
    };
    ingestion_deps.load_inventory = [this](std::string_view) {
      return inventory_;
    };
    ingestion_deps.repository_root = [this]() {
      return temp_directory_.path();
    };
    ingestion_deps.now_ms = [this]() {
      return now_ms_;
    };

    IndexWriterDeps writer_deps;
    writer_deps.snapshots_root = [this]() {
      return temp_directory_.path() / "snapshots";
    };
    writer_deps.now_ms = [this]() {
      return now_ms_;
    };
    writer_deps.record_candidate = [this](const VersionLedgerEntry& entry) {
      return ledger_->record_candidate(entry);
    };
    writer_deps.mark_active = [this](std::string_view snapshot_id, std::int64_t activated_at) {
      ++activation_count_;
      if (fail_activation_on_count_ != 0 && activation_count_ == fail_activation_on_count_) {
        return false;
      }
      return ledger_->mark_active(snapshot_id, activated_at);
    };
    writer_deps.refresh_catalog = [this](const IndexManifest& manifest) {
      block_refresh_catalog_if_needed();

      auto descriptor = descriptor_;
      descriptor.active_snapshot_id = manifest.snapshot_id;
      descriptor.last_updated_ms = manifest.effective_at;
      if (!corpus_catalog_ptr_->replace_all({descriptor})) {
        return false;
      }

      inventory_ = rebuild_inventory_from_disk();
      return true;
    };

    auto recall_coordinator = std::make_unique<RecallCoordinator>(
        RecallCoordinatorDeps{
            .sparse_lane = [this](const dasall::knowledge::retrieve::SparseRetrieveRequest& request) {
              return sparse_retriever_->retrieve(request);
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
        });

    KnowledgeServiceDeps deps;
    deps.now_ms = [this] { return now_ms_; };
    deps.query_normalizer = std::make_unique<QueryNormalizer>(make_normalize_policy());
    deps.corpus_catalog = std::move(corpus_catalog);
    deps.index_reader = std::move(index_reader);
    deps.freshness_controller = std::make_unique<dasall::knowledge::FreshnessController>();
    deps.corpus_router = std::make_unique<CorpusRouter>();
    deps.recall_coordinator = std::move(recall_coordinator);
    deps.reranker = std::make_unique<Reranker>();
    deps.evidence_assembler = std::make_unique<EvidenceAssembler>();
    deps.ingestion_coordinator = std::make_unique<IngestionCoordinator>(std::move(ingestion_deps),
                                                                        make_chunk_policy());
    deps.index_writer = std::make_unique<IndexWriter>(*index_reader_ptr_, *ledger_, writer_deps);

    service_ = std::make_unique<KnowledgeServiceFacade>(std::move(deps));
    assert_true(service_->init(config_),
                "refresh loop harness should initialize the facade with a consistent config");
  }

  void write_document(std::string_view content) {
    write_file(source_path_, content);
    now_ms_ += 1000;
  }

  [[nodiscard]] RefreshResult refresh(const CorpusChangeSet& changes) {
    now_ms_ += 1000;
    return service_->request_refresh(changes);
  }

  [[nodiscard]] KnowledgeRetrieveResult retrieve(std::string request_id,
                                                 std::string query_text) {
    const auto result = service_->retrieve(make_query(std::move(request_id), std::move(query_text)));
    assert_true(result.has_consistent_values(),
                "refresh loop retrieve should preserve result invariants");
    return result;
  }

  [[nodiscard]] SparseIndexSearchResult search_active(std::string term) const {
    const auto result = index_reader_ptr_->search_sparse(make_search_request(std::move(term)));
    assert_true(result.has_consistent_values(),
                "refresh loop sparse search should preserve result invariants");
    return result;
  }

  [[nodiscard]] dasall::knowledge::KnowledgeHealthSnapshot health_snapshot() const {
    const auto snapshot = service_->health_snapshot();
    assert_true(snapshot.has_consistent_values(),
                "refresh loop health snapshot should preserve public invariants");
    return snapshot;
  }

  [[nodiscard]] std::string current_snapshot_id() const {
    const auto manifest = index_reader_ptr_->current_manifest();
    return manifest.has_value() ? manifest->snapshot_id : std::string();
  }

  void block_next_catalog_refresh() {
    std::scoped_lock lock(block_mutex_);
    should_block_catalog_refresh_ = true;
    catalog_refresh_blocked_ = false;
    catalog_refresh_released_ = false;
  }

  [[nodiscard]] bool wait_until_catalog_refresh_blocked() {
    std::unique_lock lock(block_mutex_);
    return block_cv_.wait_for(lock, std::chrono::seconds(2), [this] {
      return catalog_refresh_blocked_;
    });
  }

  void release_catalog_refresh() {
    std::scoped_lock lock(block_mutex_);
    catalog_refresh_released_ = true;
    block_cv_.notify_all();
  }

  void fail_next_activation() {
    fail_activation_on_count_ = activation_count_ + 1;
  }

 private:
  [[nodiscard]] std::vector<SourceRecord> rebuild_inventory_from_disk() const {
    const SourceScanner scanner(SourceScannerDeps{
        .lookup_corpus = [this](std::string_view corpus_id) {
          return corpus_catalog_ptr_->snapshot().find_by_id(corpus_id);
        },
        .load_inventory = [](std::string_view) {
          return std::vector<SourceRecord>{};
        },
        .repository_root = [this]() {
          return temp_directory_.path();
        },
        .now_ms = [this]() {
          return now_ms_;
        },
    });

    CorpusScanPlan plan;
    plan.corpus_id = descriptor_.corpus_id;
    plan.root_uri = descriptor_.source_uri;
    plan.source_kind = descriptor_.source_kind;
    plan.include_globs = descriptor_.include_globs;
    plan.exclude_globs = descriptor_.exclude_globs;
    plan.allowed_formats = descriptor_.allowed_formats;
    plan.full_scan = true;

    const auto delta = scanner.scan(plan);
    assert_true(delta.has_consistent_values(),
                "refresh loop inventory rebuild should return a consistent scan delta");
    return delta.added;
  }

  void block_refresh_catalog_if_needed() {
    std::unique_lock lock(block_mutex_);
    if (!should_block_catalog_refresh_) {
      return;
    }

    catalog_refresh_blocked_ = true;
    block_cv_.notify_all();
    block_cv_.wait(lock, [this] {
      return catalog_refresh_released_;
    });
    should_block_catalog_refresh_ = false;
    catalog_refresh_blocked_ = false;
    catalog_refresh_released_ = false;
  }

  TempDirectory temp_directory_;
  std::filesystem::path source_path_;
  KnowledgeConfigSnapshot config_;
  CorpusDescriptor descriptor_;
  std::int64_t now_ms_ = 1713657600000LL;
  std::vector<SourceRecord> inventory_;
  int activation_count_ = 0;
  int fail_activation_on_count_ = 0;
  mutable std::mutex block_mutex_;
  mutable std::condition_variable block_cv_;
  bool should_block_catalog_refresh_ = false;
  bool catalog_refresh_blocked_ = false;
  bool catalog_refresh_released_ = false;
  CorpusCatalog* corpus_catalog_ptr_ = nullptr;
  IndexReader* index_reader_ptr_ = nullptr;
  std::unique_ptr<VersionLedger> ledger_;
  std::unique_ptr<SparseRetriever> sparse_retriever_;
  std::unique_ptr<KnowledgeServiceFacade> service_;
};

void assert_refresh_accepted(const RefreshResult& result, std::string_view message) {
  assert_true(result.has_consistent_values(),
              "refresh result should preserve invariants");
  assert_equal(static_cast<int>(RefreshStatus::Accepted),
               static_cast<int>(result.status),
               std::string(message));
  assert_true(!result.refresh_id.empty(),
              "accepted refresh should expose a non-empty refresh id");
}

void assert_refresh_reaches_terminal_status(KnowledgeRefreshLoopHarness& harness,
                                            RefreshStatus expected_status,
                                            std::string_view message) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline) {
    const auto snapshot = harness.health_snapshot();
    if (!snapshot.refresh_in_flight && snapshot.last_refresh_status.has_value() &&
        *snapshot.last_refresh_status == expected_status) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  throw std::runtime_error(std::string(message));
}

void test_refresh_loop_swaps_updated_snapshot_and_retrieve_observes_new_content() {
  KnowledgeRefreshLoopHarness harness;
  harness.write_document("# ADR Refresh\n\nrefreshstableanchor baseline evidence remains searchable before update.\n");

  assert_refresh_accepted(harness.refresh(CorpusChangeSet{}),
                          "initial refresh should accept the baseline corpus");
  assert_refresh_reaches_terminal_status(harness,
                                         RefreshStatus::Completed,
                                         "initial refresh should eventually complete through the async worker");
  const auto baseline_snapshot_id = harness.current_snapshot_id();
  assert_true(!baseline_snapshot_id.empty(),
              "baseline refresh should install an active snapshot");

  const auto baseline_search = harness.search_active(std::string(kBaselineToken));
  assert_true(baseline_search.ok,
              "baseline token should be searchable after the first refresh");
  assert_equal(1, static_cast<int>(baseline_search.rows.size()),
               "baseline token should produce a single indexed hit before update");

  harness.write_document("# ADR Refresh\n\nrefreshupdatedanchor refreshed evidence must be returned after swap.\n");
  CorpusChangeSet changes;
  changes.updated_sources = {std::string(kDocumentSourceUri)};
  assert_refresh_accepted(harness.refresh(changes),
                          "updated refresh should accept the changed source");
  assert_refresh_reaches_terminal_status(harness,
                                         RefreshStatus::Completed,
                                         "updated refresh should eventually complete through the async worker");

  const auto refreshed_snapshot_id = harness.current_snapshot_id();
  assert_true(refreshed_snapshot_id != baseline_snapshot_id,
              "successful refresh should swap to a new active snapshot id");

  const auto retrieve_result = harness.retrieve("req-knowledge-refresh-success",
                                                std::string(kUpdatedToken));
  assert_true(retrieve_result.ok,
              "retrieve should succeed after the refreshed snapshot becomes active");
  assert_true(retrieve_result.mode == RetrievalMode::LexicalOnly,
              "refresh loop should stay on the lexical-only path");
  assert_true(retrieve_result.evidence.has_value(),
              "retrieve should surface an evidence bundle after refresh");
  assert_true(evidence_contains_token(*retrieve_result.evidence, kUpdatedToken),
              "next retrieve should observe the refreshed token after snapshot swap");

  const auto old_token_search = harness.search_active(std::string(kBaselineToken));
  assert_true(old_token_search.ok,
              "search envelope should remain valid for the replaced token query");
  assert_equal(0, static_cast<int>(old_token_search.rows.size()),
               "old token should disappear from the active snapshot after update");
}

void test_refresh_loop_rejects_busy_request_while_real_refresh_is_in_flight() {
  KnowledgeRefreshLoopHarness harness;
  harness.write_document("# ADR Refresh\n\nrefreshstableanchor baseline evidence remains searchable before update.\n");
  harness.block_next_catalog_refresh();

  const auto first_refresh_result = harness.refresh(CorpusChangeSet{});
  assert_refresh_accepted(first_refresh_result,
                          "first refresh should be accepted before the async worker finishes");

  assert_true(harness.wait_until_catalog_refresh_blocked(),
              "busy test should observe the first refresh blocked in catalog refresh");

  const auto in_flight_snapshot = harness.health_snapshot();
  assert_true(in_flight_snapshot.refresh_in_flight,
              "busy test should expose the first refresh as in-flight while catalog refresh is blocked");

  const auto busy_result = harness.refresh(CorpusChangeSet{});
  assert_true(busy_result.has_consistent_values(),
              "busy refresh result should preserve invariants");
  assert_equal(static_cast<int>(RefreshStatus::Busy),
               static_cast<int>(busy_result.status),
               "second refresh should be rejected as busy while the first refresh is in flight");

  harness.release_catalog_refresh();
  assert_refresh_reaches_terminal_status(harness,
                                         RefreshStatus::Completed,
                                         "first refresh should still complete successfully after busy rejection");
}

void test_refresh_loop_rolls_back_to_last_known_good_when_swap_activation_fails() {
  KnowledgeRefreshLoopHarness harness;
  harness.write_document("# ADR Refresh\n\nrefreshstableanchor baseline evidence remains searchable before update.\n");
  assert_refresh_accepted(harness.refresh(CorpusChangeSet{}),
                          "initial refresh should accept the baseline corpus");
  assert_refresh_reaches_terminal_status(harness,
                                         RefreshStatus::Completed,
                                         "baseline refresh should complete before the failure-path update runs");
  const auto baseline_snapshot_id = harness.current_snapshot_id();

  harness.write_document("# ADR Refresh\n\nrefreshupdatedanchor refreshed evidence must be returned after swap.\n");
  harness.fail_next_activation();

  CorpusChangeSet changes;
  changes.updated_sources = {std::string(kDocumentSourceUri)};
  const auto accepted_refresh = harness.refresh(changes);
  assert_refresh_accepted(accepted_refresh,
                          "activation-failure path should still accept the refresh job before the worker runs");
  assert_refresh_reaches_terminal_status(harness,
                                         RefreshStatus::Failed,
                                         "activation failure should surface as a failed terminal refresh status");
  assert_equal(baseline_snapshot_id,
               harness.current_snapshot_id(),
               "failed activation must leave the last-known-good snapshot active");

  const auto stable_search = harness.search_active(std::string(kBaselineToken));
  assert_true(stable_search.ok,
              "last-known-good token should remain searchable after rollback");
  assert_equal(1, static_cast<int>(stable_search.rows.size()),
               "rollback should preserve the baseline token in the active snapshot");

  const auto candidate_search = harness.search_active(std::string(kUpdatedToken));
  assert_true(candidate_search.ok,
              "candidate search envelope should remain valid after rollback");
  assert_equal(0, static_cast<int>(candidate_search.rows.size()),
               "failed candidate snapshot must not pollute the active snapshot");
}

}  // namespace

int main() {
  try {
    test_refresh_loop_swaps_updated_snapshot_and_retrieve_observes_new_content();
    test_refresh_loop_rejects_busy_request_while_real_refresh_is_in_flight();
    test_refresh_loop_rolls_back_to_last_known_good_when_swap_activation_fails();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}