#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "ingest/Canonicalizer.h"

namespace dasall::knowledge::ingest {

enum class ChunkStrategy {
  FixedSize,
  Semantic,
  DocumentAware,
};

struct ChunkPolicy {
  ChunkStrategy strategy = ChunkStrategy::FixedSize;
  std::size_t target_chunk_chars = 512U;
  std::size_t max_chunk_chars = 768U;
  std::size_t overlap_chars = 96U;
  std::size_t min_chunk_chars = 128U;

  [[nodiscard]] bool has_consistent_values() const;
};

struct TextSpan {
  std::size_t begin = 0U;
  std::size_t end = 0U;

  [[nodiscard]] bool has_consistent_values() const;
};

struct ChunkRecord {
  std::string chunk_id;
  std::string document_id;
  std::string corpus_id;
  std::string source_id;
  std::string source_uri;
  std::string chunk_text;
  std::string version;
  std::int64_t updated_at_ms = 0;
  SourceFormat source_format = SourceFormat::Markdown;
  AuthorityLevel authority_level = AuthorityLevel::Reference;
  std::string language = "und";
  std::size_t token_estimate = 0U;
  std::size_t span_begin = 0U;
  std::size_t span_end = 0U;
  std::string citation_ref;
  std::vector<std::string> tags;
  std::vector<std::string> adjacent_chunk_refs;
  std::map<std::string, std::string> metadata;

  [[nodiscard]] bool has_consistent_values() const;
};

class Chunker {
 public:
  explicit Chunker(ChunkPolicy policy = {});

  [[nodiscard]] std::vector<ChunkRecord> chunk(const CanonicalDocument& document) const;

 private:
  [[nodiscard]] std::vector<TextSpan> split_into_spans(const CanonicalDocument& document) const;
  [[nodiscard]] std::string build_chunk_id(const CanonicalDocument& document,
                                           const TextSpan& span) const;

  ChunkPolicy policy_;
};

}  // namespace dasall::knowledge::ingest