#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "ingest/Canonicalizer.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::SourceFormat;
using dasall::knowledge::SourceKind;
using dasall::knowledge::ingest::CanonicalizeResult;
using dasall::knowledge::ingest::Canonicalizer;
using dasall::knowledge::ingest::SourceRecord;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class TempDirectory {
 public:
  TempDirectory()
      : path_(std::filesystem::temp_directory_path() /
              "dasall-canonicalizer-fallback-test") {
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

void assert_warning_present(const CanonicalizeResult& result, const std::string& warning) {
  assert_true(std::find(result.warnings.begin(), result.warnings.end(), warning) != result.warnings.end(),
              "expected warning missing: " + warning);
}

[[nodiscard]] SourceRecord make_markdown_source(std::string source_uri) {
  SourceRecord source;
  source.source_id = std::string("adr_normative::") + source_uri;
  source.corpus_id = "adr_normative";
  source.source_uri = std::move(source_uri);
  source.content_hash = "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
  source.version = std::string("sha256:") + source.content_hash;
  source.updated_at_ms = 1713657600000;
  source.kind = SourceKind::File;
  source.format = SourceFormat::Markdown;
  source.authority_level = AuthorityLevel::Normative;
  source.language = "und";
  source.tags = {"normative"};
  return source;
}

void test_canonicalizer_applies_fallbacks_and_surfaces_warnings() {
  TempDirectory temp_directory;
  const auto repository_root = temp_directory.path();
  write_file(repository_root / "docs/adr/ADR-001.md",
             "# Recovery Flow\n\nThe runtime should recover gracefully.\n");

  Canonicalizer canonicalizer({.repository_root = repository_root});
  const auto result = canonicalizer.canonicalize(make_markdown_source("docs/adr/ADR-001.md"));

  assert_true(result.has_consistent_values() && result.ok,
              "Canonicalizer should keep fallback output consistent when markdown metadata is missing");
  assert_equal("Recovery Flow", result.document->title,
               "Canonicalizer should fall back title to the first heading");
  assert_equal("adr", result.document->metadata.at("document_class"),
               "Canonicalizer should derive document_class from corpus baseline when front matter is missing");
  assert_equal("Recovery Flow", result.document->metadata.at("section_path"),
               "Canonicalizer should derive section_path from heading when front matter is missing");
  assert_equal("und", result.document->language,
               "Canonicalizer should preserve und when no explicit language metadata exists");
  assert_warning_present(result, "title_fallback_heading");
  assert_warning_present(result, "language_fallback_und");
  assert_warning_present(result, "document_class_fallback_corpus");
  assert_warning_present(result, "section_path_fallback_heading");
}

void test_canonicalizer_quarantines_empty_markdown_documents() {
  TempDirectory temp_directory;
  const auto repository_root = temp_directory.path();
  write_file(repository_root / "docs/adr/ADR-empty.md", "  \r\n\t\r\n");

  Canonicalizer canonicalizer({.repository_root = repository_root});
  const auto result = canonicalizer.canonicalize(make_markdown_source("docs/adr/ADR-empty.md"));

  assert_true(result.has_consistent_values() && !result.ok,
              "Canonicalizer should return a consistent quarantine result when canonical text is empty");
  assert_true(!result.document.has_value(),
              "quarantined canonicalization should not return a half-valid document");
  assert_equal("canonical_text_empty", *result.quarantine_reason,
               "empty markdown content should be quarantined explicitly");
}

}  // namespace

int main() {
  try {
    test_canonicalizer_applies_fallbacks_and_surfaces_warnings();
    test_canonicalizer_quarantines_empty_markdown_documents();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}