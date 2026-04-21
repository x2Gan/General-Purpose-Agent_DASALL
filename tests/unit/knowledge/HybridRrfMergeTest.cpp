#include <algorithm>
#include <exception>
#include <iostream>

#include "health/FreshnessController.h"
#include "rerank/Reranker.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::FreshnessSnapshot;
using dasall::knowledge::FreshnessState;
using dasall::knowledge::rerank::RerankPolicy;
using dasall::knowledge::rerank::Reranker;
using dasall::knowledge::retrieve::RecallCandidateSet;
using dasall::knowledge::retrieve::RecallHit;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] RecallHit make_hit(std::string chunk_id, float score, AuthorityLevel authority_level) {
  RecallHit hit;
  hit.corpus_id = "architecture-reference";
  hit.document_id = "doc-" + chunk_id;
  hit.chunk_id = std::move(chunk_id);
  hit.score = score;
  hit.raw_snippet = "snippet";
  hit.citation_ref = "docs/architecture#L1";
  hit.updated_at = 1234;
  hit.authority_level = authority_level;
  hit.tags = {"architecture"};
  return hit;
}

[[nodiscard]] FreshnessSnapshot make_freshness_snapshot() {
  FreshnessSnapshot snapshot;
  snapshot.state = FreshnessState::Fresh;
  snapshot.age_ms = 1000;
  snapshot.reason_codes = {"within_refresh_interval"};
  return snapshot;
}

void test_reranker_merges_sparse_and_dense_hits_with_rrf_and_deduplicates_by_chunk_id() {
  const Reranker reranker;
  RecallCandidateSet candidates;
  candidates.sparse_hits = {
      make_hit("chunk-a", 0.90F, AuthorityLevel::Reference),
      make_hit("chunk-b", 0.80F, AuthorityLevel::Normative),
  };
  candidates.dense_hits = {
      make_hit("chunk-b", 0.95F, AuthorityLevel::Normative),
      make_hit("chunk-c", 0.70F, AuthorityLevel::Advisory),
  };
  candidates.sparse_succeeded = true;
  candidates.dense_succeeded = true;

  RerankPolicy policy;
  policy.top_k = 3U;
  policy.rrf_k = 1U;

  const auto ranked_hits = reranker.rerank(candidates, make_freshness_snapshot(), policy);

  assert_true(ranked_hits.has_consistent_values(),
              "hybrid rerank result should stay internally consistent");
  assert_true(!ranked_hits.degraded,
              "valid hybrid rerank should not degrade");
  assert_equal(3, static_cast<int>(ranked_hits.hits.size()),
               "RRF rerank should retain the expected number of unique chunks");
  assert_equal(std::string("chunk-b"), ranked_hits.hits.at(0).hit.chunk_id,
               "RRF rerank should promote the chunk that appears in both sparse and dense lanes");
  assert_true(std::find(ranked_hits.hits.at(0).score_reason_codes.begin(),
                        ranked_hits.hits.at(0).score_reason_codes.end(),
                        "rrf_multi_lane") != ranked_hits.hits.at(0).score_reason_codes.end(),
              "multi-lane fused hit should record the rrf_multi_lane reason");
}

}  // namespace

int main() {
  try {
    test_reranker_merges_sparse_and_dense_hits_with_rrf_and_deduplicates_by_chunk_id();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}