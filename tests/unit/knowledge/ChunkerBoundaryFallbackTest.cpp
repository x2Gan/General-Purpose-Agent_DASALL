#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "ingest/Chunker.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::SourceFormat;
using dasall::knowledge::ingest::CanonicalDocument;
using dasall::knowledge::ingest::ChunkPolicy;
using dasall::knowledge::ingest::ChunkStrategy;
using dasall::knowledge::ingest::Chunker;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] CanonicalDocument make_document(std::string canonical_text) {
  CanonicalDocument document;
  document.document_id = "doc:1111111111111111111111111111111111111111111111111111111111111111";
  document.corpus_id = "ssot_normative";
  document.source_id = "ssot_normative::docs/ssot/recovery.md";
  document.source_uri = "docs/ssot/recovery.md";
  document.title = "Recovery SSOT";
  document.canonical_text = std::move(canonical_text);
  document.source_hash = "2222222222222222222222222222222222222222222222222222222222222222";
  document.version = "sha256:3333333333333333333333333333333333333333333333333333333333333333";
  document.updated_at_ms = 1713657600000;
  document.source_format = SourceFormat::Markdown;
  document.authority_level = AuthorityLevel::Normative;
  document.language = "en";
  document.tags = {"recovery", "ssot"};
  document.metadata = {
      {"document_class", "ssot"},
      {"section_path", "runtime/recovery"},
  };
  return document;
}

void test_chunker_returns_empty_for_empty_canonical_text() {
  const Chunker chunker({
      .strategy = ChunkStrategy::FixedSize,
      .target_chunk_chars = 64U,
      .max_chunk_chars = 96U,
      .overlap_chars = 8U,
      .min_chunk_chars = 16U,
  });

  const auto chunks = chunker.chunk(make_document(""));
  assert_true(chunks.empty(), "empty canonical text should yield an empty chunk list instead of a fake chunk");
}

void test_chunker_force_splits_long_paragraphs_with_deterministic_overlap() {
  const ChunkPolicy policy{
      .strategy = ChunkStrategy::FixedSize,
      .target_chunk_chars = 48U,
      .max_chunk_chars = 64U,
      .overlap_chars = 12U,
      .min_chunk_chars = 24U,
  };
  const Chunker chunker(policy);

  const auto chunks = chunker.chunk(make_document(
      "This paragraph is intentionally long so that the chunker must apply deterministic forced splitting while still preserving overlap and contiguous citation spans across the generated chunk sequence."));

  assert_true(chunks.size() >= 3U,
              "a long single paragraph should be force-split into multiple chunks when no natural breakpoint fits inside max_chunk_chars");
  for (const auto& chunk : chunks) {
    assert_true(chunk.span_end - chunk.span_begin <= policy.max_chunk_chars,
                "forced split chunks should never exceed max_chunk_chars");
  }
  assert_true(chunks[1].span_begin < chunks[0].span_end,
              "overlap should make later chunks start before the previous chunk ends");
  assert_equal(chunks.back().span_end,
               make_document(
                   "This paragraph is intentionally long so that the chunker must apply deterministic forced splitting while still preserving overlap and contiguous citation spans across the generated chunk sequence.")
                   .canonical_text.size(),
               "the last chunk should cover the tail of the canonical text exactly");
}

void test_chunker_rejects_invalid_policy() {
  bool threw = false;
  try {
    const Chunker chunker({
        .strategy = ChunkStrategy::FixedSize,
        .target_chunk_chars = 32U,
        .max_chunk_chars = 32U,
        .overlap_chars = 32U,
        .min_chunk_chars = 8U,
    });
    static_cast<void>(chunker);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  assert_true(threw, "invalid chunk policy should fail closed at construction time");
}

}  // namespace

int main() {
  try {
    test_chunker_returns_empty_for_empty_canonical_text();
    test_chunker_force_splits_long_paragraphs_with_deterministic_overlap();
    test_chunker_rejects_invalid_policy();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}