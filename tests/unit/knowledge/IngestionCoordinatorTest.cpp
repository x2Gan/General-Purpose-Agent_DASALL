#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "index/CorpusCatalog.h"
#include "ingest/IngestionCoordinator.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::CorpusChangeSet;
using dasall::knowledge::CorpusDescriptor;
using dasall::knowledge::RetrievalMode;
using dasall::knowledge::SourceFormat;
using dasall::knowledge::SourceKind;
using dasall::knowledge::TrustLevel;
using dasall::knowledge::index::CorpusCatalog;
using dasall::knowledge::ingest::ChunkStrategy;
using dasall::knowledge::ingest::IngestionCoordinator;
using dasall::knowledge::ingest::IngestionCoordinatorDeps;
using dasall::knowledge::ingest::SourceRecord;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class TempDirectory {
 public:
  TempDirectory()
      : path_(std::filesystem::temp_directory_path() /
              "dasall-ingestion-coordinator-batch-test") {
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
  descriptor.active_snapshot_id = "snapshot-adr-v1";
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

[[nodiscard]] SourceRecord make_previous_record(std::string source_uri,
                                                std::string content_hash) {
  SourceRecord record;
  record.source_id = std::string("adr_normative::") + source_uri;
  record.corpus_id = "adr_normative";
  record.source_uri = std::move(source_uri);
  record.content_hash = std::move(content_hash);
  record.version = std::string("sha256:") + record.content_hash;
  record.updated_at_ms = 42;
  record.kind = SourceKind::File;
  record.format = SourceFormat::Markdown;
  record.authority_level = AuthorityLevel::Normative;
  record.language = "zh-CN";
  record.tags = {"adr", "normative"};
  return record;
}

void test_ingestion_coordinator_builds_update_batch_and_removed_lineage_refs() {
  TempDirectory temp_directory;
  write_file(temp_directory.path() / "docs/adr/ADR-NEW.md",
             "# ADR New\n\nThis document should become chunk records.\n");

  CorpusCatalog catalog;
  assert_true(catalog.replace_all({make_descriptor()}),
              "catalog bootstrap should succeed for a trusted ADR corpus");

  IngestionCoordinatorDeps deps;
  deps.load_catalog_snapshot = [&catalog]() {
    return catalog.snapshot();
  };
  deps.load_inventory = [](std::string_view corpus_id) {
    if (corpus_id == "adr_normative") {
      return std::vector<SourceRecord>{make_previous_record(
          "docs/adr/ADR-OLD.md",
          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")};
    }
    return std::vector<SourceRecord>{};
  };
  deps.repository_root = [&temp_directory]() {
    return temp_directory.path();
  };
  deps.now_ms = []() {
    return 1713657600000LL;
  };

  const IngestionCoordinator coordinator(std::move(deps),
                                         {.strategy = ChunkStrategy::FixedSize,
                                          .target_chunk_chars = 80U,
                                          .max_chunk_chars = 120U,
                                          .overlap_chars = 0U,
                                          .min_chunk_chars = 24U});

  const auto batch = coordinator.build_update_batch(CorpusChangeSet{});

  assert_true(batch.has_consistent_values(),
              "full-scan ingestion should emit a consistent index update batch");
  assert_true(!batch.chunk_records.empty(),
              "full-scan ingestion should produce chunk records for the current trusted sources");
  assert_equal(1, static_cast<int>(batch.removed_document_ids.size()),
               "full-scan ingestion should emit one removal lineage ref for inventory-only sources");
  assert_true(batch.removed_document_ids.front().rfind("doclineage:", 0) == 0,
              "removed document refs should use deterministic source-lineage ids");
  assert_true(std::none_of(batch.warnings.begin(), batch.warnings.end(), [](const auto& warning) {
                return warning.find("quarantine") != std::string::npos;
              }),
              "healthy full-scan ingestion should not emit quarantine-class warnings");
  assert_true(batch.chunk_records.front().metadata.contains("document_lineage_id"),
              "chunk metadata should carry a deterministic document_lineage_id");
  assert_equal(std::string("adr_normative::docs/adr/ADR-NEW.md"),
               batch.chunk_records.front().source_id,
               "chunk provenance should come from the scanned and canonicalized source");
}

}  // namespace

int main() {
  try {
    test_ingestion_coordinator_builds_update_batch_and_removed_lineage_refs();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}