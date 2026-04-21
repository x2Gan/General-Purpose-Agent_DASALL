#include <atomic>
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
using dasall::knowledge::ingest::SourceScanner;
using dasall::knowledge::ingest::SourceScannerDeps;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class TempDirectory {
 public:
  TempDirectory() : path_(std::filesystem::temp_directory_path() / make_name()) {
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
  [[nodiscard]] static std::string make_name() {
    static std::atomic<std::uint64_t> counter{0U};
    return std::string("dasall-source-scanner-test-") +
           std::to_string(counter.fetch_add(1U));
  }

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
  descriptor.exclude_globs = {"draft-*.md"};
  descriptor.supported_modes = {RetrievalMode::LexicalOnly};
  descriptor.active_snapshot_id = "snapshot-001";
  descriptor.last_updated_ms = 1713657600000;
  descriptor.tags = {"normative", "adr"};
  descriptor.metadata = {{"baseline_class", "architecture"},
                         {"owner_module", "knowledge"},
                         {"refresh_strategy", "manual"},
                         {"default_language", "zh-CN"}};
  return descriptor;
}

[[nodiscard]] SourceScanner make_scanner(const std::filesystem::path& repository_root,
                                         const CorpusDescriptor& descriptor) {
  SourceScannerDeps deps;
  deps.lookup_corpus = [descriptor](std::string_view corpus_id) -> std::optional<CorpusDescriptor> {
    if (corpus_id == descriptor.corpus_id) {
      return descriptor;
    }

    return std::nullopt;
  };
  deps.load_inventory = [](std::string_view) {
    return std::vector<dasall::knowledge::ingest::SourceRecord>{};
  };
  deps.repository_root = [repository_root]() {
    return repository_root;
  };
  deps.now_ms = []() {
    return 1713657600000LL;
  };
  return SourceScanner(std::move(deps));
}

void test_source_scanner_discovers_trusted_markdown_sources_as_added_records() {
  TempDirectory temp_directory;
  const auto repository_root = temp_directory.path();
  write_file(repository_root / "docs/adr/ADR-001.md", "# ADR-001\npolicy text\n");
  write_file(repository_root / "docs/adr/ADR-002.md", "# ADR-002\nmore evidence\n");
  write_file(repository_root / "docs/adr/README.txt", "not part of include globs\n");

  const auto scanner = make_scanner(repository_root, make_descriptor());

  CorpusScanPlan plan;
  plan.corpus_id = "adr_normative";
  plan.root_uri = "docs/adr";
  plan.source_kind = SourceKind::File;
  plan.include_globs = {"ADR-*.md"};
  plan.allowed_formats = {SourceFormat::Markdown};

  const auto delta = scanner.scan(plan);
  assert_true(delta.has_consistent_values(),
              "SourceScanner should emit a consistent delta for a trusted corpus full scan");
  assert_true(delta.full_scan,
              "empty inventory should force SourceScanner to mark the scan as full_scan");
  assert_equal(2, static_cast<int>(delta.added.size()),
               "SourceScanner should surface each matching markdown file as an added source");
  assert_equal(0, static_cast<int>(delta.updated.size()),
               "first scan should not surface updated sources");
  assert_equal(0, static_cast<int>(delta.removed_source_ids.size()),
               "first scan should not surface removed sources");
  assert_equal(0, static_cast<int>(delta.quarantined_source_ids.size()),
               "matching markdown sources should not be quarantined");
  assert_equal("adr_normative::docs/adr/ADR-001.md", delta.added.front().source_id,
               "SourceScanner should build a stable source id from corpus id and repo relative path");
  assert_equal("zh-CN", delta.added.front().language,
               "SourceScanner should inherit default language from the corpus descriptor");
  assert_true(delta.added.front().has_consistent_values(),
              "added source records should carry stable provenance fields");
}

}  // namespace

int main() {
  try {
    test_source_scanner_discovers_trusted_markdown_sources_as_added_records();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}