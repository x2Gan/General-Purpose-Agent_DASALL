#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "ingest/Chunker.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::SourceFormat;
using dasall::knowledge::ingest::CanonicalDocument;
using dasall::knowledge::ingest::ChunkPolicy;
using dasall::knowledge::ingest::ChunkRecord;
using dasall::knowledge::ingest::ChunkStrategy;
using dasall::knowledge::ingest::Chunker;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] CanonicalDocument make_document(std::string canonical_text) {
  CanonicalDocument document;
  document.document_id = "doc:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  document.corpus_id = "architecture_reference";
  document.source_id = "architecture_reference::docs/architecture/chunker.md";
  document.source_uri = "docs/architecture/chunker.md";
  document.title = "Chunker Design";
  document.canonical_text = std::move(canonical_text);
  document.source_hash = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
  document.version = "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
  document.updated_at_ms = 1713657600000;
  document.source_format = SourceFormat::Markdown;
  document.authority_level = AuthorityLevel::Reference;
  document.language = "zh-CN";
  document.tags = {"architecture", "knowledge"};
  document.metadata = {
      {"document_class", "architecture"},
      {"section_path", "runtime/chunker"},
  };
  return document;
}

void assert_chunk_sequence_consistent(const std::vector<ChunkRecord>& chunks) {
  assert_true(!chunks.empty(), "chunk sequence should not be empty");
  assert_true(std::all_of(chunks.begin(), chunks.end(), [](const auto& chunk) {
                return chunk.has_consistent_values();
              }),
              "every generated chunk should satisfy ChunkRecord invariants");
}

void test_chunker_prefers_heading_and_paragraph_boundaries_and_inherits_provenance() {
  const Chunker chunker({
      .strategy = ChunkStrategy::FixedSize,
      .target_chunk_chars = 96U,
      .max_chunk_chars = 120U,
      .overlap_chars = 0U,
      .min_chunk_chars = 32U,
  });

  const auto document = make_document(
      "# Intro\n\n"
      "This first paragraph explains how the knowledge ingest stage preserves deterministic document structure.\n\n"
      "## Recovery\n\n"
      "This second paragraph explains how the runtime preserves audit-ready recovery evidence under failure pressure.");

  const auto chunks = chunker.chunk(document);

  assert_true(chunks.size() >= 2U,
              "heading and paragraph-aware chunking should produce multiple chunks for multi-section markdown");
  assert_chunk_sequence_consistent(chunks);
  assert_equal(document.document_id, chunks.front().document_id,
               "chunk provenance should inherit document_id from CanonicalDocument");
  assert_equal(document.corpus_id, chunks.front().corpus_id,
               "chunk provenance should inherit corpus_id from CanonicalDocument");
  assert_equal(document.metadata.at("document_class"), chunks.front().metadata.at("document_class"),
               "chunk metadata should preserve document_class");
  assert_equal(document.metadata.at("section_path"), chunks.front().metadata.at("section_path"),
               "chunk metadata should preserve section_path");
  assert_true(chunks.front().chunk_text.find("# Intro") != std::string::npos,
              "first chunk should keep the opening heading");
  assert_true(std::any_of(chunks.begin() + 1, chunks.end(), [](const auto& chunk) {
                return chunk.chunk_text.find("## Recovery") != std::string::npos;
              }),
              "some downstream chunk should preserve the later heading boundary");
  assert_true(chunks.front().citation_ref.rfind(document.source_uri + "#char=", 0) == 0,
              "citation_ref should be derived deterministically from source_uri and span offsets");
  assert_true(chunks.front().adjacent_chunk_refs.size() == 1U &&
                  chunks.front().adjacent_chunk_refs.front() == chunks[1].chunk_id,
              "first chunk should reference its next adjacent chunk id");
  assert_true(chunks.back().adjacent_chunk_refs.size() == 1U &&
                  chunks.back().adjacent_chunk_refs.front() == chunks[chunks.size() - 2U].chunk_id,
              "last chunk should reference its previous adjacent chunk id");
}

}  // namespace

int main() {
  try {
    test_chunker_prefers_heading_and_paragraph_boundaries_and_inherits_provenance();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}