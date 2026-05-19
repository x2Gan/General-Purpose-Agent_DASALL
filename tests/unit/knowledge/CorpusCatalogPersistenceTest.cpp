#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "index/CorpusCatalog.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::CorpusDescriptor;
using dasall::knowledge::RetrievalMode;
using dasall::knowledge::SourceFormat;
using dasall::knowledge::SourceKind;
using dasall::knowledge::TrustLevel;
using dasall::knowledge::index::CorpusCatalog;
using dasall::knowledge::index::CorpusCatalogDeps;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

struct TempDirectory {
  explicit TempDirectory(std::string name)
      : path(std::filesystem::temp_directory_path() / std::move(name)) {
    std::error_code error;
    std::filesystem::remove_all(path, error);
    std::filesystem::create_directories(path);
  }

  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }

  std::filesystem::path path;
};

[[nodiscard]] std::string slurp_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void write_file(const std::filesystem::path& path, std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << content;
}

[[nodiscard]] CorpusDescriptor make_descriptor(std::string corpus_id,
                                              std::string display_name,
                                              std::string source_uri,
                                              std::string active_snapshot_id,
                                              std::vector<std::string> tags) {
  CorpusDescriptor descriptor;
  descriptor.corpus_id = std::move(corpus_id);
  descriptor.display_name = std::move(display_name);
  descriptor.source_uri = std::move(source_uri);
  descriptor.trust_level = TrustLevel::Trusted;
  descriptor.authority_level = AuthorityLevel::Normative;
  descriptor.source_kind = SourceKind::File;
  descriptor.allowed_formats = {SourceFormat::Markdown, SourceFormat::Text};
  descriptor.include_globs = {"*.md", "*.txt"};
  descriptor.exclude_globs = {"*.tmp"};
  descriptor.supported_modes = {RetrievalMode::LexicalOnly, RetrievalMode::Hybrid};
  descriptor.active_snapshot_id = std::move(active_snapshot_id);
  descriptor.last_updated_ms = 1713657600000;
  descriptor.tags = std::move(tags);
  descriptor.metadata = {
      {"baseline_class", "trusted_corpus"},
      {"owner_module", "knowledge"},
      {"refresh_strategy", "startup_restore"},
      {"default_language", "zh-CN"},
  };
  return descriptor;
}

void test_corpus_catalog_restores_descriptors_from_persisted_snapshot() {
  TempDirectory temp_directory("dasall-corpus-catalog-persistence-test");
  const auto catalog_path = temp_directory.path / "corpus_catalog.json";

  CorpusCatalog catalog(CorpusCatalogDeps{.catalog_path = catalog_path});
  assert_true(catalog.replace_all({
                  make_descriptor("adr_normative", "ADR Normative", "docs/adr/",
                                  "snapshot-adr-v2", {"normative", "architecture"}),
                  make_descriptor("ssot_normative", "SSOT Normative", "docs/ssot/",
                                  "snapshot-ssot-v1", {"normative", "ssot"}),
              }),
              "catalog should persist a valid descriptor baseline to disk");

  CorpusCatalog reloaded_catalog(CorpusCatalogDeps{.catalog_path = catalog_path});
  const auto snapshot = reloaded_catalog.snapshot();
  assert_true(snapshot.has_consistent_values(),
              "reloaded catalog snapshot should remain internally consistent");
  assert_equal(2, static_cast<int>(snapshot.size()),
               "reloaded catalog should recover all persisted descriptors");

  const auto adr_descriptor = snapshot.find_by_id("adr_normative");
  assert_true(adr_descriptor.has_value(),
              "reloaded catalog should recover descriptors by corpus id");
  assert_equal("snapshot-adr-v2", adr_descriptor->active_snapshot_id,
               "reloaded catalog should preserve the active snapshot id");
  assert_equal(2, static_cast<int>(adr_descriptor->allowed_formats.size()),
               "reloaded catalog should preserve vector-like route metadata");
  assert_equal("startup_restore", adr_descriptor->metadata.at("refresh_strategy"),
               "reloaded catalog should preserve persisted metadata fields");
}

void test_corpus_catalog_fails_closed_on_unknown_persistence_format() {
  TempDirectory temp_directory("dasall-corpus-catalog-format-test");
  const auto catalog_path = temp_directory.path / "corpus_catalog.json";

  CorpusCatalog catalog(CorpusCatalogDeps{.catalog_path = catalog_path});
  assert_true(catalog.replace_all({make_descriptor("adr_normative", "ADR Normative",
                                                   "docs/adr/", "snapshot-adr-v2",
                                                   {"normative"})}),
              "catalog should write an initial persisted snapshot before format mutation");

  auto file_content = slurp_file(catalog_path);
  const auto version_marker = file_content.find("\"catalog_format_version\":1");
  assert_true(version_marker != std::string::npos,
              "persisted catalog should include the expected format version marker");
  file_content.replace(version_marker,
                       std::string{"\"catalog_format_version\":1"}.size(),
                       "\"catalog_format_version\":99");
  write_file(catalog_path, file_content);

  CorpusCatalog reloaded_catalog(CorpusCatalogDeps{.catalog_path = catalog_path});
  assert_true(reloaded_catalog.snapshot().empty(),
              "unknown catalog format version must fail closed instead of trusting stale descriptors");
}

}  // namespace

int main() {
  try {
    test_corpus_catalog_restores_descriptors_from_persisted_snapshot();
    test_corpus_catalog_fails_closed_on_unknown_persistence_format();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}