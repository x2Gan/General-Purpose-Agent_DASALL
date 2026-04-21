#include <algorithm>
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
using dasall::tests::support::assert_true;

class TempDirectory {
 public:
  TempDirectory()
      : path_(std::filesystem::temp_directory_path() /
              "dasall-ingestion-coordinator-selective-test") {
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

[[nodiscard]] CorpusDescriptor make_descriptor(std::string corpus_id,
                                              std::string display_name,
                                              std::string source_uri) {
  CorpusDescriptor descriptor;
  descriptor.corpus_id = std::move(corpus_id);
  descriptor.display_name = std::move(display_name);
  descriptor.source_uri = std::move(source_uri);
  descriptor.trust_level = TrustLevel::Trusted;
  descriptor.authority_level = AuthorityLevel::Normative;
  descriptor.source_kind = SourceKind::File;
  descriptor.allowed_formats = {SourceFormat::Markdown};
  descriptor.include_globs = {"*.md"};
  descriptor.exclude_globs = {};
  descriptor.supported_modes = {RetrievalMode::LexicalOnly};
  descriptor.active_snapshot_id = "snapshot-current";
  descriptor.last_updated_ms = 1713657600000;
  descriptor.tags = {"knowledge"};
  descriptor.metadata = {
      {"baseline_class", "trusted_corpus"},
      {"owner_module", "knowledge"},
      {"refresh_strategy", "manual"},
      {"default_language", "zh-CN"},
  };
  return descriptor;
}

[[nodiscard]] SourceRecord make_previous_record(std::string corpus_id,
                                                std::string source_uri,
                                                std::string content_hash) {
  SourceRecord record;
  record.source_id = corpus_id + "::" + source_uri;
  record.corpus_id = std::move(corpus_id);
  record.source_uri = std::move(source_uri);
  record.content_hash = std::move(content_hash);
  record.version = std::string("sha256:") + record.content_hash;
  record.updated_at_ms = 24;
  record.kind = SourceKind::File;
  record.format = SourceFormat::Markdown;
  record.authority_level = AuthorityLevel::Normative;
  record.language = "zh-CN";
  record.tags = {"knowledge"};
  return record;
}

void test_ingestion_coordinator_only_refreshes_impacted_corpora() {
  TempDirectory temp_directory;
  write_file(temp_directory.path() / "docs/adr/ADR-007.md",
             "# ADR 007\n\nUpdated recovery policy text.\n");
  write_file(temp_directory.path() / "docs/ssot/Matrix.md",
             "# Matrix\n\nThis should not be scanned during selective ADR refresh.\n");

  CorpusCatalog catalog;
  assert_true(catalog.replace_all({
                  make_descriptor("adr_normative", "ADR Normative", "docs/adr/"),
                  make_descriptor("ssot_normative", "SSOT Normative", "docs/ssot/"),
              }),
              "catalog bootstrap should succeed for selective refresh coverage");

  IngestionCoordinatorDeps deps;
  deps.load_catalog_snapshot = [&catalog]() {
    return catalog.snapshot();
  };
  deps.load_inventory = [](std::string_view corpus_id) {
    if (corpus_id == "adr_normative") {
      return std::vector<SourceRecord>{make_previous_record(
          "adr_normative", "docs/adr/ADR-007.md",
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

  const auto batch = coordinator.build_update_batch(CorpusChangeSet{
      .added_sources = {},
      .updated_sources = {"docs/adr/ADR-007.md"},
      .removed_sources = {},
  });

  assert_true(batch.has_consistent_values(),
              "selective refresh should still emit a consistent update batch");
  assert_true(!batch.chunk_records.empty(),
              "selective refresh should produce chunk records for the impacted corpus");
  assert_true(std::all_of(batch.chunk_records.begin(), batch.chunk_records.end(), [](const auto& chunk) {
                return chunk.corpus_id == "adr_normative";
              }),
              "non-impacted corpora must not be rescanned during selective refresh");
  assert_true(std::none_of(batch.chunk_records.begin(), batch.chunk_records.end(), [](const auto& chunk) {
                return chunk.source_uri == "docs/ssot/Matrix.md";
              }),
              "non-target corpora should not leak chunk records into a selective refresh batch");
}

}  // namespace

int main() {
  try {
    test_ingestion_coordinator_only_refreshes_impacted_corpora();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}