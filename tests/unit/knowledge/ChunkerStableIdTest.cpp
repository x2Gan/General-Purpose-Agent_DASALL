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
using dasall::knowledge::ingest::ChunkStrategy;
using dasall::knowledge::ingest::Chunker;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] CanonicalDocument make_document(std::string title,
                                             std::string canonical_text) {
  CanonicalDocument document;
  document.document_id = "doc:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
  document.corpus_id = "adr_normative";
  document.source_id = "adr_normative::docs/adr/ADR-007.md";
  document.source_uri = "docs/adr/ADR-007.md";
  document.title = std::move(title);
  document.canonical_text = std::move(canonical_text);
  document.source_hash = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
  document.version = "sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
  document.updated_at_ms = 1713657600000;
  document.source_format = SourceFormat::Markdown;
  document.authority_level = AuthorityLevel::Normative;
  document.language = "zh-CN";
  document.tags = {"adr", "normative"};
  document.metadata = {
      {"document_class", "adr"},
      {"section_path", "runtime/recovery"},
  };
  return document;
}

void test_chunker_keeps_chunk_ids_stable_for_identical_canonical_inputs() {
  const ChunkPolicy policy{
      .strategy = ChunkStrategy::FixedSize,
      .target_chunk_chars = 72U,
      .max_chunk_chars = 96U,
      .overlap_chars = 12U,
      .min_chunk_chars = 24U,
  };
  const Chunker chunker(policy);

  const auto first_document = make_document(
      "ADR 007",
      "# Recovery Manager\n\nThe recovery manager owns admission control for recovery actions.\n\nIt must reject retry storms and conflicting remediation plans.");

  auto second_document = first_document;
  second_document.title = "ADR 007 Updated Title";
  second_document.updated_at_ms += 4096;
  second_document.tags = {"adr", "governance", "normative"};

  const auto first_chunks = chunker.chunk(first_document);
  const auto second_chunks = chunker.chunk(second_document);

  assert_true(first_chunks.size() == second_chunks.size() && !first_chunks.empty(),
              "stable-id comparison requires two non-empty chunk sequences with identical boundaries");
  for (std::size_t index = 0; index < first_chunks.size(); ++index) {
    assert_equal(first_chunks[index].chunk_id, second_chunks[index].chunk_id,
                 "chunk ids should remain stable when canonical text, document_id and policy do not change");
    assert_equal(first_chunks[index].citation_ref, second_chunks[index].citation_ref,
                 "citation refs should remain stable when span boundaries do not change");
  }
}

}  // namespace

int main() {
  try {
    test_chunker_keeps_chunk_ids_stable_for_identical_canonical_inputs();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}