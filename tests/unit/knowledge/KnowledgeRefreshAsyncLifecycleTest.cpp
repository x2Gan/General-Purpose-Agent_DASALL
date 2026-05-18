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
#include <vector>

#include "facade/KnowledgeService.h"
#include "index/IndexReader.h"
#include "index/IndexWriter.h"
#include "index/VersionLedger.h"
#include "ingest/IngestionCoordinator.h"
#include "retrieve/SparseRetriever.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::CorpusChangeSet;
using dasall::knowledge::CorpusDescriptor;
using dasall::knowledge::IndexManifest;
using dasall::knowledge::KnowledgeConfigSnapshot;
using dasall::knowledge::RefreshStatus;
using dasall::knowledge::RetrievalMode;
using dasall::knowledge::SourceFormat;
using dasall::knowledge::SourceKind;
using dasall::knowledge::TrustLevel;
using dasall::knowledge::facade::KnowledgeServiceDeps;
using dasall::knowledge::facade::KnowledgeServiceFacade;
using dasall::knowledge::index::CorpusCatalog;
using dasall::knowledge::index::IndexReader;
using dasall::knowledge::index::IndexWriter;
using dasall::knowledge::index::IndexWriterDeps;
using dasall::knowledge::index::VersionLedger;
using dasall::knowledge::index::VersionLedgerDeps;
using dasall::knowledge::index::VersionLedgerEntry;
using dasall::knowledge::ingest::ChunkStrategy;
using dasall::knowledge::ingest::IngestionCoordinator;
using dasall::knowledge::ingest::IngestionCoordinatorDeps;
using dasall::knowledge::ingest::SourceRecord;
using dasall::knowledge::retrieve::SparseIndexSearchRequest;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

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

[[nodiscard]] CorpusDescriptor make_descriptor() {
  CorpusDescriptor descriptor;
  descriptor.corpus_id = "adr_normative";
  descriptor.display_name = "ADR Normative";
  descriptor.source_uri = "docs/adr/";
  descriptor.trust_level = TrustLevel::Trusted;
  descriptor.authority_level = AuthorityLevel::Normative;
  descriptor.source_kind = SourceKind::File;
  descriptor.allowed_formats = {SourceFormat::Markdown};
  descriptor.include_globs = {"ADR-*.md"};
  descriptor.exclude_globs = {};
  descriptor.supported_modes = {RetrievalMode::LexicalOnly};
  descriptor.active_snapshot_id = "snapshot-adr-bootstrap";
  descriptor.last_updated_ms = 1713657600000;
  descriptor.tags = {"adr", "normative"};
  descriptor.metadata = {
      {"baseline_class", "trusted_corpus"},
      {"owner_module", "knowledge"},
      {"refresh_strategy", "manual"},
      {"default_language", "zh-CN"},
  };
  return descriptor;
}

[[nodiscard]] SparseIndexSearchRequest make_search_request() {
  SparseIndexSearchRequest request;
  request.expression.match_expression = "\"policy\"";
  request.expression.lexical_terms = {"policy"};
  request.allowed_corpus_ids = {"adr_normative"};
  request.minimum_authority_level = AuthorityLevel::Reference;
  request.top_k = 4U;
  return request;
}

class AsyncRefreshHarness {
 public:
  AsyncRefreshHarness()
      : temp_directory_("dasall-knowledge-refresh-async-test") {
    write_file(temp_directory_.path() / "docs/adr/ADR-ASYNC.md",
               "# ADR Async Refresh\n\npolicy evidence after refresh.\n");

    auto corpus_catalog = std::make_unique<CorpusCatalog>();
    assert_true(corpus_catalog->replace_all({make_descriptor()}),
                "catalog bootstrap should succeed for the async refresh test");
    corpus_catalog_ptr_ = corpus_catalog.get();

    auto index_reader = std::make_unique<IndexReader>();
    index_reader_ptr_ = index_reader.get();

    ledger_ = std::make_unique<VersionLedger>(VersionLedgerDeps{
        .read_snapshot_checksum = [this](std::string_view snapshot_id) {
          return index_reader_ptr_->read_snapshot_checksum(snapshot_id);
        },
    });

    IngestionCoordinatorDeps ingestion_deps;
    ingestion_deps.load_catalog_snapshot = [this]() {
      return corpus_catalog_ptr_->snapshot();
    };
    ingestion_deps.load_inventory = [](std::string_view) {
      return std::vector<SourceRecord>{};
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
    writer_deps.mark_active = [this](std::string_view snapshot_id,
                                     std::int64_t activated_at) {
      if (fail_next_activation_) {
        fail_next_activation_ = false;
        return false;
      }
      return ledger_->mark_active(snapshot_id, activated_at);
    };
    writer_deps.refresh_catalog = [this](const IndexManifest& manifest) {
      block_refresh_catalog_if_needed();

      auto descriptor = make_descriptor();
      descriptor.active_snapshot_id = manifest.snapshot_id;
      descriptor.last_updated_ms = manifest.effective_at;
      return corpus_catalog_ptr_->replace_all({descriptor});
    };

    KnowledgeServiceDeps deps;
    deps.corpus_catalog = std::move(corpus_catalog);
    deps.index_reader = std::move(index_reader);
    deps.ingestion_coordinator = std::make_unique<IngestionCoordinator>(
        std::move(ingestion_deps),
        dasall::knowledge::ingest::ChunkPolicy{
            .strategy = ChunkStrategy::FixedSize,
            .target_chunk_chars = 80U,
            .max_chunk_chars = 120U,
            .overlap_chars = 0U,
            .min_chunk_chars = 24U,
        });
    deps.index_writer =
        std::make_unique<IndexWriter>(*index_reader_ptr_, *ledger_, writer_deps);

    service_ = std::make_unique<KnowledgeServiceFacade>(std::move(deps));
    assert_true(service_->init(make_config()),
                "async refresh harness should initialize before issuing refresh requests");
  }

  [[nodiscard]] dasall::knowledge::RefreshResult request_refresh(const CorpusChangeSet& changes) {
    now_ms_ += 1000;
    return service_->request_refresh(changes);
  }

  [[nodiscard]] dasall::knowledge::KnowledgeHealthSnapshot health_snapshot() const {
    return service_->health_snapshot();
  }

  [[nodiscard]] std::optional<IndexManifest> current_manifest() const {
    return index_reader_ptr_->current_manifest();
  }

  [[nodiscard]] bool active_snapshot_contains_policy() const {
    const auto result = index_reader_ptr_->search_sparse(make_search_request());
    assert_true(result.has_consistent_values(),
                "async refresh sparse search should preserve search result invariants");
    return result.ok && !result.rows.empty();
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
    fail_next_activation_ = true;
  }

 private:
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
  std::int64_t now_ms_ = 1713657600000LL;
  mutable std::mutex block_mutex_;
  mutable std::condition_variable block_cv_;
  bool should_block_catalog_refresh_ = false;
  bool catalog_refresh_blocked_ = false;
  bool catalog_refresh_released_ = false;
  bool fail_next_activation_ = false;
  CorpusCatalog* corpus_catalog_ptr_ = nullptr;
  IndexReader* index_reader_ptr_ = nullptr;
  std::unique_ptr<VersionLedger> ledger_;
  std::unique_ptr<KnowledgeServiceFacade> service_;
};

[[nodiscard]] bool wait_for_terminal_status(AsyncRefreshHarness& harness,
                                            RefreshStatus expected_status) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline) {
    const auto health_snapshot = harness.health_snapshot();
    if (!health_snapshot.refresh_in_flight &&
        health_snapshot.last_refresh_status.has_value() &&
        *health_snapshot.last_refresh_status == expected_status) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

void test_async_refresh_reports_in_flight_busy_and_completed_terminal_status() {
  AsyncRefreshHarness harness;
  harness.block_next_catalog_refresh();

  const auto accepted = harness.request_refresh(CorpusChangeSet{});
  assert_true(accepted.has_consistent_values(),
              "async refresh request should preserve public refresh result invariants");
  assert_equal(static_cast<int>(RefreshStatus::Accepted),
               static_cast<int>(accepted.status),
               "async refresh should return Accepted before the real refresh finishes");
  assert_true(!accepted.refresh_id.empty(),
              "accepted async refresh should expose a non-empty job id");

  assert_true(harness.wait_until_catalog_refresh_blocked(),
              "async refresh should reach the blocked catalog refresh checkpoint");

  const auto in_flight_snapshot = harness.health_snapshot();
  assert_true(in_flight_snapshot.refresh_in_flight,
              "health snapshot should expose the refresh as in-flight while the worker is blocked");
  assert_true(!in_flight_snapshot.last_refresh_status.has_value(),
              "health snapshot should not report a terminal refresh status before the worker completes");

  const auto busy = harness.request_refresh(CorpusChangeSet{});
  assert_true(busy.has_consistent_values(),
              "busy refresh result should preserve public refresh invariants");
  assert_equal(static_cast<int>(RefreshStatus::Busy),
               static_cast<int>(busy.status),
               "second refresh should be rejected as Busy while the async worker owns the slot");

  harness.release_catalog_refresh();

  assert_true(wait_for_terminal_status(harness, RefreshStatus::Completed),
              "health snapshot should expose Completed as the last terminal refresh status after completion");
  assert_true(harness.current_manifest().has_value(),
              "completed async refresh should leave an active snapshot installed");
  assert_true(harness.active_snapshot_contains_policy(),
              "completed async refresh should still index the scanned corpus content");
}

void test_async_refresh_surfaces_failed_terminal_status_after_worker_failure() {
  AsyncRefreshHarness harness;
  harness.fail_next_activation();

  const auto accepted = harness.request_refresh(CorpusChangeSet{});
  assert_true(accepted.status == RefreshStatus::Accepted,
              "failing async refresh should still return Accepted when the job is admitted");

  assert_true(wait_for_terminal_status(harness, RefreshStatus::Failed),
              "health snapshot should surface Failed once the async worker finishes with an activation error");
  assert_true(!harness.current_manifest().has_value(),
              "failed async refresh should not publish a partially activated snapshot");
}

}  // namespace

int main() {
  try {
    test_async_refresh_reports_in_flight_busy_and_completed_terminal_status();
    test_async_refresh_surfaces_failed_terminal_status_after_worker_failure();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}