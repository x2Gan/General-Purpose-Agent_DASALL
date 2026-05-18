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
using dasall::knowledge::CorpusDescriptor;
using dasall::knowledge::FreshnessState;
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
  descriptor.active_snapshot_id.clear();
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

class InitPrewarmHarness {
 public:
  InitPrewarmHarness()
      : temp_directory_("dasall-knowledge-init-prewarm-test") {
    write_file(temp_directory_.path() / "docs/adr/ADR-PREWARM.md",
               "# ADR Init Prewarm\n\npolicy evidence available after init prewarm.\n");
  }

  [[nodiscard]] KnowledgeServiceDeps make_deps(bool fail_next_activation = false) {
    auto corpus_catalog = std::make_unique<CorpusCatalog>();
    assert_true(corpus_catalog->replace_all({make_descriptor()}),
                "catalog bootstrap should succeed for the init prewarm test");
    auto* corpus_catalog_ptr = corpus_catalog.get();

    auto index_reader = std::make_unique<IndexReader>();
    index_reader_ptr_ = index_reader.get();

    auto ledger = std::make_shared<VersionLedger>(VersionLedgerDeps{
        .read_snapshot_checksum = [this](std::string_view snapshot_id) {
          return index_reader_ptr_->read_snapshot_checksum(snapshot_id);
        },
    });

    fail_next_activation_ = fail_next_activation;

    IngestionCoordinatorDeps ingestion_deps;
    ingestion_deps.load_catalog_snapshot = [corpus_catalog_ptr]() {
      return corpus_catalog_ptr->snapshot();
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
    writer_deps.record_candidate = [ledger](const auto& entry) {
      return ledger->record_candidate(entry);
    };
    writer_deps.mark_active = [this, ledger](std::string_view snapshot_id,
                                             std::int64_t activated_at) {
      if (fail_next_activation_) {
        fail_next_activation_ = false;
        return false;
      }
      return ledger->mark_active(snapshot_id, activated_at);
    };
    writer_deps.refresh_catalog = [corpus_catalog_ptr](const IndexManifest& manifest) {
      auto descriptor = make_descriptor();
      descriptor.active_snapshot_id = manifest.snapshot_id;
      descriptor.last_updated_ms = manifest.effective_at;
      return corpus_catalog_ptr->replace_all({descriptor});
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
    deps.index_writer = std::make_unique<IndexWriter>(*index_reader_ptr_, *ledger, writer_deps);
    deps.startup_prewarm_on_init = true;
    return deps;
  }

  [[nodiscard]] std::optional<IndexManifest> current_manifest() const {
    return index_reader_ptr_->current_manifest();
  }

  [[nodiscard]] bool active_snapshot_contains_policy() const {
    const auto result = index_reader_ptr_->search_sparse(make_search_request());
    assert_true(result.has_consistent_values(),
                "init prewarm sparse search should preserve search result invariants");
    return result.ok && !result.rows.empty();
  }

 private:
  TempDirectory temp_directory_;
  std::int64_t now_ms_ = 1713657602000LL;
  bool fail_next_activation_ = false;
  IndexReader* index_reader_ptr_ = nullptr;
};

void test_init_runs_synchronous_prewarm_when_active_snapshot_is_missing() {
  InitPrewarmHarness harness;
  KnowledgeServiceFacade facade(harness.make_deps());

  assert_true(facade.init(make_config()),
              "facade init should synchronously prewarm when startup policy requires an initial build");

  const auto health_snapshot = facade.health_snapshot();
  assert_true(!health_snapshot.refresh_in_flight,
              "startup prewarm should settle before init returns");
  assert_true(health_snapshot.last_refresh_status.has_value() &&
                  *health_snapshot.last_refresh_status == RefreshStatus::Completed,
              "startup prewarm should surface Completed as the terminal refresh status");

  const auto manifest = harness.current_manifest();
  assert_true(manifest.has_value(),
              "startup prewarm should publish an active snapshot during init");
  assert_true(!manifest->snapshot_id.empty(),
              "startup prewarm should publish a non-empty active snapshot id");
  assert_true(harness.active_snapshot_contains_policy(),
              "startup prewarm should index the scanned corpus content before init returns");
}

void test_init_fails_closed_when_startup_prewarm_cannot_activate_snapshot() {
  InitPrewarmHarness harness;
  KnowledgeServiceFacade facade(harness.make_deps(true));

  assert_true(!facade.init(make_config()),
              "facade init should fail closed when the required startup prewarm cannot activate a snapshot");

  const auto health_snapshot = facade.health_snapshot();
  assert_true(!health_snapshot.refresh_in_flight,
              "failed startup prewarm should not leave the facade in-flight after init returns");
  assert_true(health_snapshot.last_refresh_status.has_value() &&
                  *health_snapshot.last_refresh_status == RefreshStatus::Failed,
              "failed startup prewarm should surface Failed as the last refresh status");
  assert_true(!harness.current_manifest().has_value(),
              "failed startup prewarm must not publish a partial active snapshot");
}

}  // namespace

int main() {
  try {
    test_init_runs_synchronous_prewarm_when_active_snapshot_is_missing();
    test_init_fails_closed_when_startup_prewarm_cannot_activate_snapshot();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}