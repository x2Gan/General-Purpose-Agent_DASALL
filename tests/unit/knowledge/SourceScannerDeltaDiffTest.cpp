#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "KnowledgeTypes.h"
#include "ingest/SourceScanner.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::CorpusDescriptor;
using dasall::knowledge::RetrievalMode;
using dasall::knowledge::SourceFormat;
using dasall::knowledge::SourceKind;
using dasall::knowledge::TrustLevel;
using dasall::knowledge::ingest::CorpusScanPlan;
using dasall::knowledge::ingest::SourceRecord;
using dasall::knowledge::ingest::SourceScanner;
using dasall::knowledge::ingest::SourceScannerDeps;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class TempDirectory {
 public:
  TempDirectory() : path_(std::filesystem::temp_directory_path() / "dasall-source-scanner-diff-test") {
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
  descriptor.source_uri = "docs/adr";
  descriptor.trust_level = TrustLevel::Trusted;
  descriptor.authority_level = AuthorityLevel::Normative;
  descriptor.source_kind = SourceKind::File;
  descriptor.allowed_formats = {SourceFormat::Markdown};
  descriptor.include_globs = {"ADR-*.md"};
  descriptor.exclude_globs = {};
  descriptor.supported_modes = {RetrievalMode::LexicalOnly};
  descriptor.active_snapshot_id = "snapshot-002";
  descriptor.last_updated_ms = 1713657600000;
  descriptor.tags = {"normative", "adr"};
  descriptor.metadata = {{"baseline_class", "architecture"},
                         {"owner_module", "knowledge"},
                         {"refresh_strategy", "manual"},
                         {"default_language", "zh-CN"}};
  return descriptor;
}

[[nodiscard]] SourceRecord make_previous_record(std::string source_uri,
                                                std::string content_hash,
                                                std::int64_t updated_at_ms) {
  SourceRecord record;
  record.source_id = std::string("adr_normative::") + source_uri;
  record.corpus_id = "adr_normative";
  record.source_uri = std::move(source_uri);
  record.content_hash = std::move(content_hash);
  record.version = std::string("sha256:") + record.content_hash;
  record.updated_at_ms = updated_at_ms;
  record.kind = SourceKind::File;
  record.format = SourceFormat::Markdown;
  record.authority_level = AuthorityLevel::Normative;
  record.language = "zh-CN";
  record.tags = {"normative", "adr"};
  return record;
}

void test_source_scanner_emits_added_updated_and_removed_deltas_from_inventory_diff() {
  TempDirectory temp_directory;
  const auto repository_root = temp_directory.path();
  write_file(repository_root / "docs/adr/ADR-001.md", "# ADR-001\nupdated policy\n");
  write_file(repository_root / "docs/adr/ADR-003.md", "# ADR-003\nnew evidence\n");

  const auto descriptor = make_descriptor();
  const std::vector<SourceRecord> previous_inventory = {
      make_previous_record("docs/adr/ADR-001.md",
                           "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                           10),
      make_previous_record("docs/adr/ADR-002.md",
                           "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                           20),
  };

  SourceScannerDeps deps;
  deps.lookup_corpus = [descriptor](std::string_view corpus_id) -> std::optional<CorpusDescriptor> {
    if (corpus_id == descriptor.corpus_id) {
      return descriptor;
    }

    return std::nullopt;
  };
  deps.load_inventory = [previous_inventory](std::string_view) {
    return previous_inventory;
  };
  deps.repository_root = [repository_root]() {
    return repository_root;
  };
  deps.now_ms = []() {
    return 1713657600000LL;
  };

  SourceScanner scanner(std::move(deps));

  CorpusScanPlan plan;
  plan.corpus_id = "adr_normative";
  plan.root_uri = "docs/adr";
  plan.source_kind = SourceKind::File;
  plan.include_globs = {"ADR-*.md"};
  plan.allowed_formats = {SourceFormat::Markdown};

  const auto delta = scanner.scan(plan);
  assert_true(delta.has_consistent_values(),
              "SourceScanner should keep diff output consistent when inventory is present");
  assert_true(!delta.full_scan,
              "non-empty inventory without explicit full_scan should keep SourceScanner in diff mode");
  assert_equal(1, static_cast<int>(delta.added.size()),
               "new sources should land in the added bucket");
  assert_equal(1, static_cast<int>(delta.updated.size()),
               "changed sources should land in the updated bucket");
  assert_equal(1, static_cast<int>(delta.removed_source_ids.size()),
               "missing sources from previous inventory should land in the removed bucket");
  assert_equal("adr_normative::docs/adr/ADR-003.md", delta.added.front().source_id,
               "SourceScanner should mark unseen current sources as added");
  assert_equal("adr_normative::docs/adr/ADR-001.md", delta.updated.front().source_id,
               "SourceScanner should mark hash/version/update-time changes as updated");
  assert_equal("adr_normative::docs/adr/ADR-002.md", delta.removed_source_ids.front(),
               "SourceScanner should mark inventory-only sources as removed");
}

}  // namespace

int main() {
  try {
    test_source_scanner_emits_added_updated_and_removed_deltas_from_inventory_diff();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}