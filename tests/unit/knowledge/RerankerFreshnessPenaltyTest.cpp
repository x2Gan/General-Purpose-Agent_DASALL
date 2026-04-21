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
using dasall::tests::support::assert_true;

[[nodiscard]] RecallHit make_hit(std::string chunk_id, AuthorityLevel authority_level) {
  RecallHit hit;
  hit.corpus_id = "architecture-reference";
  hit.document_id = "doc-" + chunk_id;
  hit.chunk_id = std::move(chunk_id);
  hit.score = 0.90F;
  hit.raw_snippet = "snippet";
  hit.citation_ref = "docs/architecture#L1";
  hit.updated_at = 1234;
  hit.authority_level = authority_level;
  hit.tags = {"architecture"};
  return hit;
}

[[nodiscard]] FreshnessSnapshot make_snapshot(FreshnessState state, bool stale_read_allowed) {
  FreshnessSnapshot snapshot;
  snapshot.state = state;
  snapshot.age_ms = state == FreshnessState::Fresh ? 1000 : 50000;
  snapshot.stale_read_allowed = stale_read_allowed;
  snapshot.rebuild_recommended = state != FreshnessState::Fresh;
  snapshot.reason_codes = {state == FreshnessState::Fresh ? "within_refresh_interval"
                                                          : "refresh_interval_elapsed"};
  return snapshot;
}

void test_reranker_applies_stale_penalty_to_ranked_hits() {
  const Reranker reranker;
  RecallCandidateSet candidates;
  candidates.sparse_hits = {make_hit("chunk-normative", AuthorityLevel::Normative)};
  candidates.sparse_succeeded = true;

  const auto fresh_hits = reranker.rerank(candidates,
                                          make_snapshot(FreshnessState::Fresh, false),
                                          RerankPolicy{});
  const auto stale_hits = reranker.rerank(candidates,
                                          make_snapshot(FreshnessState::StaleAllowed, true),
                                          RerankPolicy{});

  assert_true(fresh_hits.hits.at(0).fused_score > stale_hits.hits.at(0).fused_score,
              "stale snapshot should reduce the final fused score relative to the fresh path");
  assert_true(stale_hits.hits.at(0).stale,
              "stale route should mark ranked hits as stale");
}

void test_reranker_applies_authority_weighting_when_raw_scores_are_equal() {
  const Reranker reranker;
  RecallCandidateSet candidates;
  candidates.sparse_hits = {make_hit("chunk-normative", AuthorityLevel::Normative)};
  candidates.dense_hits = {make_hit("chunk-advisory", AuthorityLevel::Advisory)};
  candidates.sparse_succeeded = true;
  candidates.dense_succeeded = true;

  auto ranked_hits = reranker.rerank(candidates,
                                     make_snapshot(FreshnessState::Fresh, false),
                                     RerankPolicy{});

  assert_true(ranked_hits.hits.at(0).hit.authority_level == AuthorityLevel::Normative,
              "normative hit should outrank an equally fused advisory hit due to authority weighting");
  assert_true(ranked_hits.hits.at(0).fused_score > ranked_hits.hits.at(1).fused_score,
              "authority weighting should create a strict score delta between normative and advisory hits");
}

}  // namespace

int main() {
  try {
    test_reranker_applies_stale_penalty_to_ranked_hits();
    test_reranker_applies_authority_weighting_when_raw_scores_are_equal();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}