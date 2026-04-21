#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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
using dasall::tests::support::assert_true;

class TempDirectory {
 public:
  TempDirectory()
      : path_(std::filesystem::temp_directory_path() /
              "dasall-ingestion-coordinator-bad-source-test") {
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

void test_ingestion_coordinator_emits_warning_for_bad_source_without_aborting_batch() {
  TempDirectory temp_directory;
  write_file(temp_directory.path() / "docs/adr/ADR-001.md",
             "# ADR 001\n\nHealthy source content.\n");
  write_file(temp_directory.path() / "docs/adr/ADR-EMPTY.md", "");

  CorpusCatalog catalog;
  assert_true(catalog.replace_all({make_descriptor()}),
              "catalog bootstrap should succeed for bad-source coverage");

  IngestionCoordinatorDeps deps;
  deps.load_catalog_snapshot = [&catalog]() {
    return catalog.snapshot();
  };
  deps.load_inventory = [](std::string_view) {
    return std::vector<dasall::knowledge::ingest::SourceRecord>{};
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
              "a bad source should still yield a consistent batch with warnings");
  assert_true(!batch.chunk_records.empty(),
              "a single bad source must not abort chunk generation for healthy sources");
  assert_true(std::all_of(batch.chunk_records.begin(), batch.chunk_records.end(), [](const auto& chunk) {
                return chunk.source_id == "adr_normative::docs/adr/ADR-001.md";
              }),
              "only healthy sources should produce chunk records when an empty source is quarantined");
  assert_true(std::any_of(batch.warnings.begin(), batch.warnings.end(), [](const auto& warning) {
                return warning.find("ADR-EMPTY.md") != std::string::npos;
              }),
              "bad sources should surface as batch warnings instead of silent drops");
}

}  // namespace

int main() {
  try {
    test_ingestion_coordinator_emits_warning_for_bad_source_without_aborting_batch();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}