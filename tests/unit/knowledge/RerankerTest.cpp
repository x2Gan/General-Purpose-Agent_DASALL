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

[[nodiscard]] RecallHit make_hit(std::string chunk_id, float score) {
  RecallHit hit;
  hit.corpus_id = "architecture-reference";
  hit.document_id = "doc-" + chunk_id;
  hit.chunk_id = std::move(chunk_id);
  hit.score = score;
  hit.raw_snippet = "snippet";
  hit.citation_ref = "docs/architecture#L1";
  hit.updated_at = 1234;
  hit.authority_level = AuthorityLevel::Reference;
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

void test_reranker_accepts_empty_candidate_sets_as_legal_result() {
  const Reranker reranker;
  const RecallCandidateSet candidates;

  const auto ranked_hits = reranker.rerank(candidates, make_freshness_snapshot(), RerankPolicy{});

  assert_true(ranked_hits.has_consistent_values(),
              "empty rerank result should still satisfy RankedHitSet invariants");
  assert_true(ranked_hits.hits.empty(),
              "empty candidate set should produce an empty RankedHitSet");
  assert_true(!ranked_hits.degraded,
              "empty candidate set should be a legal result, not an implicit degraded path");
}

void test_reranker_falls_back_to_lexical_first_when_policy_is_invalid() {
  const Reranker reranker;
  RecallCandidateSet candidates;
  candidates.sparse_hits = {make_hit("chunk-a", 0.8F), make_hit("chunk-b", 0.7F)};
  candidates.dense_hits = {make_hit("chunk-c", 0.9F)};
  candidates.sparse_succeeded = true;
  candidates.dense_succeeded = true;

  RerankPolicy invalid_policy;
  invalid_policy.top_k = 0U;

  const auto ranked_hits = reranker.rerank(candidates, make_freshness_snapshot(), invalid_policy);

  assert_true(ranked_hits.has_consistent_values(),
              "fallback rerank result should stay internally consistent");
  assert_true(ranked_hits.degraded,
              "invalid rerank policy should trigger the lexical-first degraded fallback");
  assert_equal(std::string("chunk-a"), ranked_hits.hits.at(0).hit.chunk_id,
               "lexical-first fallback should preserve sparse lane order first");
  assert_equal(std::string("chunk-b"), ranked_hits.hits.at(1).hit.chunk_id,
               "lexical-first fallback should keep remaining sparse hits before dense-only hits");
  assert_equal(std::string("chunk-c"), ranked_hits.hits.at(2).hit.chunk_id,
               "lexical-first fallback should append dense-only hits after sparse lane output");
}

}  // namespace

int main() {
  try {
    test_reranker_accepts_empty_candidate_sets_as_legal_result();
    test_reranker_falls_back_to_lexical_first_when_policy_is_invalid();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}