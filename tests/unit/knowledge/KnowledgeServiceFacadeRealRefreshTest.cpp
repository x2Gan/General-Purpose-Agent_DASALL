#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
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

void test_facade_runs_real_refresh_via_ingestion_and_index_writer_owners() {
  TempDirectory temp_directory("dasall-knowledge-facade-refresh-test");
  write_file(temp_directory.path() / "docs/adr/ADR-REAL.md",
             "# ADR Real Refresh\n\npolicy evidence after refresh.\n");

  auto corpus_catalog = std::make_unique<CorpusCatalog>();
  assert_true(corpus_catalog->replace_all({make_descriptor()}),
              "catalog bootstrap should succeed for the real refresh test");
  auto* corpus_catalog_ptr = corpus_catalog.get();

  auto index_reader = std::make_unique<IndexReader>();
  auto* index_reader_ptr = index_reader.get();

  auto ledger = std::make_unique<VersionLedger>(VersionLedgerDeps{
      .read_snapshot_checksum = [index_reader_ptr](std::string_view snapshot_id) {
        return index_reader_ptr->read_snapshot_checksum(snapshot_id);
      },
  });

  std::int64_t now_ms = 1713657601000LL;

  IngestionCoordinatorDeps ingestion_deps;
  ingestion_deps.load_catalog_snapshot = [corpus_catalog_ptr]() {
    return corpus_catalog_ptr->snapshot();
  };
  ingestion_deps.load_inventory = [](std::string_view) {
    return std::vector<SourceRecord>{};
  };
  ingestion_deps.repository_root = [&temp_directory]() {
    return temp_directory.path();
  };
  ingestion_deps.now_ms = [&now_ms]() {
    return now_ms;
  };

  IndexWriterDeps writer_deps;
  writer_deps.snapshots_root = [&temp_directory]() {
    return temp_directory.path() / "snapshots";
  };
  writer_deps.now_ms = [&now_ms]() {
    return now_ms;
  };
  writer_deps.refresh_catalog = [corpus_catalog_ptr](const IndexManifest& manifest) {
    auto descriptor = make_descriptor();
    descriptor.active_snapshot_id = manifest.snapshot_id;
    descriptor.last_updated_ms = manifest.effective_at;
    return corpus_catalog_ptr->replace_all({descriptor});
  };

  KnowledgeServiceDeps deps;
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
  deps.index_writer = std::make_unique<IndexWriter>(*index_reader_ptr, *ledger, writer_deps);

  KnowledgeServiceFacade facade(std::move(deps));
  assert_true(facade.init(make_config()),
              "facade should initialize before running the real refresh path");

  const auto refresh_result = facade.request_refresh_sync_for_tests(CorpusChangeSet{});
  assert_true(refresh_result.has_consistent_values(),
              "real refresh should return a consistent refresh result");
  assert_equal(static_cast<int>(RefreshStatus::Completed),
               static_cast<int>(refresh_result.status),
               "sync refresh helper should surface Completed once the real refresh finishes");
  assert_true(!refresh_result.refresh_id.empty(),
              "completed refresh should surface the deterministic batch id when tests use the sync helper");

  const auto health_snapshot = facade.health_snapshot();
  assert_true(!health_snapshot.refresh_in_flight,
              "sync test helper should leave the facade idle after refresh completion");
  assert_true(health_snapshot.last_refresh_status.has_value() &&
            *health_snapshot.last_refresh_status == RefreshStatus::Completed,
              "sync test helper should record the completed refresh status in health snapshot");

  const auto manifest = index_reader_ptr->current_manifest();
  assert_true(manifest.has_value(),
              "real refresh should leave an active index snapshot in the index reader");
  assert_true(!manifest->snapshot_id.empty(),
              "active manifest should expose a non-empty snapshot id");

  const auto search_result = index_reader_ptr->search_sparse(make_search_request());
  assert_true(search_result.ok,
              "active snapshot installed by real refresh should be searchable");
  assert_true(!search_result.rows.empty(),
              "real refresh should index the scanned corpus content into the active snapshot");
}

}  // namespace

int main() {
  try {
    test_facade_runs_real_refresh_via_ingestion_and_index_writer_owners();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}