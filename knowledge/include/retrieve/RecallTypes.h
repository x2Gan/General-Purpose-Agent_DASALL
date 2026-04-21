#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "KnowledgeTypes.h"

namespace dasall::knowledge::retrieve {

struct RecallHit {
  std::string corpus_id;
  std::string document_id;
  std::string chunk_id;
  float score = 0.0F;
  std::string raw_snippet;
  std::string citation_ref;
  std::int64_t updated_at = 0;
  AuthorityLevel authority_level = AuthorityLevel::Reference;
  std::vector<std::string> tags;

  [[nodiscard]] bool has_consistent_values() const {
    return !corpus_id.empty() && !document_id.empty() && !chunk_id.empty() &&
           score >= 0.0F && !raw_snippet.empty() && !citation_ref.empty() && updated_at >= 0 &&
           detail::has_unique_values(tags);
  }
};

struct RecallCandidateSet {
  std::vector<RecallHit> sparse_hits;
  std::vector<RecallHit> dense_hits;
  bool sparse_succeeded = false;
  bool dense_succeeded = false;
  bool degraded = false;
  std::vector<std::string> warnings;

  [[nodiscard]] bool has_consistent_values() const {
    return std::all_of(sparse_hits.begin(), sparse_hits.end(), [](const RecallHit& hit) {
             return hit.has_consistent_values();
           }) &&
           std::all_of(dense_hits.begin(), dense_hits.end(), [](const RecallHit& hit) {
             return hit.has_consistent_values();
           }) &&
           detail::has_unique_values(warnings);
  }
};

}  // namespace dasall::knowledge::retrieve