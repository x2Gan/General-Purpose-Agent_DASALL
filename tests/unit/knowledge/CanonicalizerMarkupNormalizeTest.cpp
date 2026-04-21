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
using dasall::knowledge::ingest::Canonicalizer;
using dasall::knowledge::ingest::SourceRecord;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class TempDirectory {
 public:
  TempDirectory()
      : path_(std::filesystem::temp_directory_path() /
              "dasall-canonicalizer-markup-test") {
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

[[nodiscard]] SourceRecord make_markdown_source(std::string source_uri) {
  SourceRecord source;
  source.source_id = std::string("architecture_reference::") + source_uri;
  source.corpus_id = "architecture_reference";
  source.source_uri = std::move(source_uri);
  source.content_hash = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
  source.version = std::string("sha256:") + source.content_hash;
  source.updated_at_ms = 1713657600000;
  source.kind = SourceKind::File;
  source.format = SourceFormat::Markdown;
  source.authority_level = AuthorityLevel::Reference;
  source.language = "zh-CN";
  source.tags = {"architecture"};
  return source;
}

void test_canonicalizer_normalizes_markdown_without_rewriting_structure() {
  TempDirectory temp_directory;
  const auto repository_root = temp_directory.path();
  const auto markdown_path = repository_root / "docs/architecture/Canonicalizer.md";
  write_file(markdown_path,
             "\xEF\xBB\xBF---\r\n"
             "title: Canonical Title\r\n"
             "document_class: architecture\r\n"
             "section_path: runtime/canonicalizer\r\n"
             "tags: [knowledge, canonical]\r\n"
             "---\r\n"
             "# Canonical Title\r\n"
             "\r\n"
             "- item one\r\n"
             "\r\n"
             "```cpp\r\n"
             "int value = 1;\r\n"
             "```\r\n");

  Canonicalizer canonicalizer({.repository_root = repository_root});
  const auto result = canonicalizer.canonicalize(
      make_markdown_source("docs/architecture/Canonicalizer.md"));

  assert_true(result.has_consistent_values() && result.ok,
              "Canonicalizer should normalize markdown into a consistent canonical document");
  assert_equal("Canonical Title", result.document->title,
               "front matter title should be preferred over heading fallback");
  assert_equal("architecture", result.document->metadata.at("document_class"),
               "front matter document_class should be preserved");
  assert_equal("runtime/canonicalizer", result.document->metadata.at("section_path"),
               "front matter section_path should be preserved");
  assert_equal(std::string("# Canonical Title\n\n- item one\n\n```cpp\nint value = 1;\n```"),
               result.document->canonical_text,
               "markdown canonicalization should strip front matter and normalize CRLF to LF while preserving headings, lists and code fences");
  assert_true(result.document->canonical_text.find('\r') == std::string::npos,
              "markdown canonical text should not retain carriage returns");
  assert_true(result.document->tags == std::vector<std::string>({"architecture", "canonical", "knowledge"}),
              "front matter tags should merge with source tags deterministically");
}

}  // namespace

int main() {
  try {
    test_canonicalizer_normalizes_markdown_without_rewriting_structure();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}