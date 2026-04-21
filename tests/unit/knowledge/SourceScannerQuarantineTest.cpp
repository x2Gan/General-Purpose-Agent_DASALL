#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

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
  TempDirectory() : path_(std::filesystem::temp_directory_path() / "dasall-source-scanner-quarantine-test") {
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
  descriptor.include_globs = {"*"};
  descriptor.exclude_globs = {};
  descriptor.supported_modes = {RetrievalMode::LexicalOnly};
  descriptor.active_snapshot_id = "snapshot-003";
  descriptor.last_updated_ms = 1713657600000;
  descriptor.tags = {"normative", "adr"};
  descriptor.metadata = {{"baseline_class", "architecture"},
                         {"owner_module", "knowledge"},
                         {"refresh_strategy", "manual"},
                         {"default_language", "zh-CN"}};
  return descriptor;
}

void test_source_scanner_quarantines_sources_outside_allowed_format_list() {
  TempDirectory temp_directory;
  const auto repository_root = temp_directory.path();
  write_file(repository_root / "docs/adr/ADR-001.md", "# ADR-001\npolicy text\n");
  write_file(repository_root / "docs/adr/metadata.json", "{\"unexpected\":true}\n");

  const auto descriptor = make_descriptor();

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

  SourceScanner scanner(std::move(deps));

  CorpusScanPlan plan;
  plan.corpus_id = "adr_normative";
  plan.root_uri = "docs/adr";
  plan.source_kind = SourceKind::File;
  plan.include_globs = {"*"};
  plan.allowed_formats = {SourceFormat::Markdown};

  const auto delta = scanner.scan(plan);
  assert_true(delta.has_consistent_values(),
              "SourceScanner quarantine output should remain self-consistent");
  assert_equal(1, static_cast<int>(delta.added.size()),
               "allowed markdown sources should still be scanned successfully");
  assert_equal(1, static_cast<int>(delta.quarantined_source_ids.size()),
               "non allow-list formats should be emitted as quarantined sources");
  assert_equal("adr_normative::docs/adr/metadata.json", delta.quarantined_source_ids.front(),
               "quarantine should preserve the stable source id for rejected files");
}

}  // namespace

int main() {
  try {
    test_source_scanner_quarantines_sources_outside_allowed_format_list();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}