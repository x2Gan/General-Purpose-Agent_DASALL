#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include "retrieve/RecallCoordinator.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::KnowledgeErrorCode;
using dasall::knowledge::KnowledgeQueryKind;
using dasall::knowledge::RetrievalMode;
using dasall::knowledge::make_knowledge_error_info;
using dasall::knowledge::query::NormalizedQuery;
using dasall::knowledge::query::RetrievalPlan;
using dasall::knowledge::retrieve::DenseRecallRequest;
using dasall::knowledge::retrieve::DenseRecallResult;
using dasall::knowledge::retrieve::RecallCoordinator;
using dasall::knowledge::retrieve::RecallCoordinatorDeps;
using dasall::knowledge::retrieve::RecallHit;
using dasall::knowledge::retrieve::RecallRequest;
using dasall::knowledge::retrieve::SparseRetrieveRequest;
using dasall::knowledge::retrieve::SparseRetrieveResult;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] NormalizedQuery make_query() {
  NormalizedQuery query;
  query.request_id = "req-001";
  query.normalized_text = "policy evidence";
  query.lexical_terms = {"policy", "evidence"};
  query.domain_tags = {"normative"};
  query.allowed_corpora = {"adr_normative"};
  query.query_kind = KnowledgeQueryKind::PolicyEvidence;
  query.top_k = 4U;
  query.max_context_projection_items = 4U;
  query.prefer_exact_match = true;
  return query;
}

[[nodiscard]] RetrievalPlan make_plan(RetrievalMode mode,
                                      bool allow_partial_results = false) {
  RetrievalPlan plan;
  plan.mode = mode;
  plan.corpus_ids = {"adr_normative"};
  plan.sparse_top_k = mode == RetrievalMode::DenseOnly ? 0U : 4U;
  plan.dense_top_k = mode == RetrievalMode::LexicalOnly ? 0U : 3U;
  plan.allow_partial_results = allow_partial_results;
  plan.max_projection_items = 4U;
  plan.route_reason_codes = {"route_ok"};
  return plan;
}

[[nodiscard]] RecallRequest make_request(RetrievalMode mode,
                                         bool allow_partial_results = false) {
  RecallRequest request;
  request.normalized_query = make_query();
  request.plan = make_plan(mode, allow_partial_results);
  request.required_language = "zh-CN";
  return request;
}

[[nodiscard]] RecallHit make_hit(std::string chunk_id) {
  RecallHit hit;
  hit.corpus_id = "adr_normative";
  hit.document_id = "adr-001";
  hit.chunk_id = std::move(chunk_id);
  hit.score = 0.6F;
  hit.raw_snippet = "policy evidence snippet";
  hit.citation_ref = "ADR-001#policy";
  hit.updated_at = 1713657600000;
  hit.authority_level = AuthorityLevel::Normative;
  hit.tags = {"normative"};
  return hit;
}

void test_recall_coordinator_runs_lexical_only_without_dense_lane() {
  bool dense_called = false;
  RecallCoordinator coordinator(RecallCoordinatorDeps{
      .sparse_lane = [](const SparseRetrieveRequest& request) {
        assert_true(request.has_consistent_values(),
                    "sparse lane should receive a consistent sparse request");
        SparseRetrieveResult result;
        result.ok = true;
        result.hits = {make_hit("chunk-sparse")};
        return result;
      },
      .dense_lane = [&dense_called](const DenseRecallRequest&) {
        dense_called = true;
        DenseRecallResult result;
        result.ok = false;
        result.failure_reason_codes = {"unexpected_dense_call"};
        return result;
      },
  });

  const auto result = coordinator.recall(make_request(RetrievalMode::LexicalOnly));
  assert_true(result.ok,
              "lexical-only recall should succeed when the sparse lane succeeds");
  assert_true(result.has_consistent_values(),
              "successful lexical-only recall should preserve result shape");
  assert_true(result.candidates.sparse_succeeded,
              "lexical-only recall should mark sparse lane as succeeded");
  assert_true(!result.candidates.dense_succeeded,
              "lexical-only recall should not mark dense lane as succeeded");
  assert_true(!result.candidates.degraded,
              "single sparse lane success should not degrade the candidate set");
  assert_equal(1, static_cast<int>(result.candidates.sparse_hits.size()),
               "lexical-only recall should return sparse lane hits");
  assert_true(!dense_called,
              "lexical-only recall must not call the dense lane");
}

void test_recall_coordinator_collects_both_hybrid_lanes_when_they_succeed() {
  RecallCoordinator coordinator(RecallCoordinatorDeps{
      .sparse_lane = [](const SparseRetrieveRequest&) {
        SparseRetrieveResult result;
        result.ok = true;
        result.hits = {make_hit("chunk-sparse")};
        return result;
      },
      .dense_lane = [](const DenseRecallRequest& request) {
        assert_true(request.has_consistent_values(),
                    "dense lane should receive a consistent dense request");
        DenseRecallResult result;
        result.ok = true;
        result.hits = {make_hit("chunk-dense")};
        return result;
      },
  });

  const auto result = coordinator.recall(make_request(RetrievalMode::Hybrid));
  assert_true(result.ok,
              "hybrid recall should succeed when both lanes succeed");
  assert_true(result.has_consistent_values(),
              "hybrid recall success should preserve result shape");
  assert_true(result.candidates.sparse_succeeded,
              "hybrid recall should record sparse success");
  assert_true(result.candidates.dense_succeeded,
              "hybrid recall should record dense success");
  assert_true(!result.candidates.degraded,
              "hybrid recall with both lanes succeeding should not be degraded");
  assert_equal(1, static_cast<int>(result.candidates.sparse_hits.size()),
               "hybrid recall should retain sparse hits");
  assert_equal(1, static_cast<int>(result.candidates.dense_hits.size()),
               "hybrid recall should retain dense hits");
}

}  // namespace

int main() {
  try {
    test_recall_coordinator_runs_lexical_only_without_dense_lane();
    test_recall_coordinator_collects_both_hybrid_lanes_when_they_succeed();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}