#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include "KnowledgeErrors.h"
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

[[nodiscard]] RecallHit make_hit(std::string chunk_id) {
  RecallHit hit;
  hit.corpus_id = "adr_normative";
  hit.document_id = "adr-001";
  hit.chunk_id = std::move(chunk_id);
  hit.score = 0.7F;
  hit.raw_snippet = "policy evidence snippet";
  hit.citation_ref = "ADR-001#policy";
  hit.updated_at = 1713657600000;
  hit.authority_level = AuthorityLevel::Normative;
  hit.tags = {"normative"};
  return hit;
}

[[nodiscard]] RecallRequest make_request(bool allow_partial_results) {
  NormalizedQuery query;
  query.request_id = "req-002";
  query.normalized_text = "policy evidence";
  query.lexical_terms = {"policy", "evidence"};
  query.domain_tags = {"normative"};
  query.allowed_corpora = {"adr_normative"};
  query.query_kind = KnowledgeQueryKind::PolicyEvidence;
  query.top_k = 4U;
  query.max_context_projection_items = 4U;

  RetrievalPlan plan;
  plan.mode = RetrievalMode::Hybrid;
  plan.corpus_ids = {"adr_normative"};
  plan.sparse_top_k = 4U;
  plan.dense_top_k = 3U;
  plan.allow_partial_results = allow_partial_results;
  plan.max_projection_items = 4U;
  plan.route_reason_codes = {"route_ok"};

  RecallRequest request;
  request.normalized_query = std::move(query);
  request.plan = std::move(plan);
  request.required_language = "zh-CN";
  return request;
}

void test_recall_coordinator_marks_dense_lane_failure_as_degraded_when_partial_allowed() {
  RecallCoordinator coordinator(RecallCoordinatorDeps{
      .sparse_lane = [](const SparseRetrieveRequest&) {
        SparseRetrieveResult result;
        result.ok = true;
        result.hits = {make_hit("chunk-sparse")};
        return result;
      },
      .dense_bridge = nullptr,
      .dense_lane = [](const DenseRecallRequest&) {
        DenseRecallResult result;
        result.ok = false;
        result.failure_reason_codes = {"recall_timeout"};
        return result;
      },
  });

  const auto result = coordinator.recall(make_request(true));
  assert_true(result.ok,
              "single-lane success should be accepted when partial results are allowed");
  assert_true(result.has_consistent_values(),
              "degraded success should preserve result shape");
  assert_true(result.candidates.degraded,
              "single dense lane failure should degrade the candidate set");
  assert_true(result.candidates.sparse_succeeded,
              "degraded result should retain sparse success");
  assert_true(!result.candidates.dense_succeeded,
              "degraded result should record dense lane failure");
  assert_equal(1, static_cast<int>(result.candidates.sparse_hits.size()),
               "degraded result should keep sparse hits for downstream rerank");
  assert_true(std::find(result.candidates.warnings.begin(),
                        result.candidates.warnings.end(),
                        "dense_recall_timeout") != result.candidates.warnings.end(),
              "dense timeout should surface as a stable degraded warning code");
}

void test_recall_coordinator_fails_when_both_lanes_fail() {
  RecallCoordinator coordinator(RecallCoordinatorDeps{
      .sparse_lane = [](const SparseRetrieveRequest&) {
        SparseRetrieveResult result;
        result.ok = false;
        result.error = make_knowledge_error_info(KnowledgeErrorCode::IndexUnavailable,
                                                 "index unavailable",
                                                 "recall_coordinator.sparse_lane",
                                                 "sparse_lane_failed");
        return result;
      },
      .dense_bridge = nullptr,
      .dense_lane = [](const DenseRecallRequest&) {
        DenseRecallResult result;
        result.ok = false;
        result.failure_reason_codes = {"recall_timeout"};
        return result;
      },
  });

  const auto result = coordinator.recall(make_request(false));
  assert_true(!result.ok,
              "dual-lane failure should be surfaced as an explicit coordinator failure");
  assert_true(result.has_consistent_values(),
              "dual-lane failure should still satisfy result shape");
  assert_true(!result.candidates.sparse_succeeded && !result.candidates.dense_succeeded,
              "failure result must not masquerade as a partial success");
  assert_true(result.failure_reason_codes.size() == 2U,
              "dual-lane failure should keep both lane reason codes");
  assert_true(std::any_of(result.failure_reason_codes.begin(),
                          result.failure_reason_codes.end(),
                          [](const std::string& reason_code) {
                            return reason_code.rfind("sparse_index_unavailable", 0) == 0;
                          }),
              "sparse lane failure should preserve provider reason code");
  assert_true(std::find(result.failure_reason_codes.begin(),
                        result.failure_reason_codes.end(),
                        "dense_recall_timeout") != result.failure_reason_codes.end(),
              "dense lane failure should preserve timeout reason code");
}

}  // namespace

int main() {
  try {
    test_recall_coordinator_marks_dense_lane_failure_as_degraded_when_partial_allowed();
    test_recall_coordinator_fails_when_both_lanes_fail();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}