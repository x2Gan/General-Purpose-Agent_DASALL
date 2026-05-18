#include <chrono>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

#include "retrieve/RecallCoordinator.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::KnowledgeQueryKind;
using dasall::knowledge::RetrievalMode;
using dasall::knowledge::query::NormalizedQuery;
using dasall::knowledge::query::RetrievalPlan;
using dasall::knowledge::retrieve::DenseRecallRequest;
using dasall::knowledge::retrieve::DenseRecallResult;
using dasall::knowledge::retrieve::RecallCoordinator;
using dasall::knowledge::retrieve::RecallCoordinatorDeps;
using dasall::knowledge::retrieve::RecallCoordinatorPolicy;
using dasall::knowledge::retrieve::RecallHit;
using dasall::knowledge::retrieve::RecallRequest;
using dasall::knowledge::retrieve::SparseRetrieveRequest;
using dasall::knowledge::retrieve::SparseRetrieveResult;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

using namespace std::chrono_literals;

[[nodiscard]] RecallHit make_hit(std::string chunk_id) {
  RecallHit hit;
  hit.corpus_id = "adr_normative";
  hit.document_id = "adr-001";
  hit.chunk_id = std::move(chunk_id);
  hit.score = 0.72F;
  hit.raw_snippet = "policy evidence snippet";
  hit.citation_ref = "ADR-001#policy";
  hit.updated_at = 1713657600000;
  hit.authority_level = AuthorityLevel::Normative;
  hit.tags = {"normative"};
  return hit;
}

[[nodiscard]] RecallRequest make_request() {
  NormalizedQuery query;
  query.request_id = "req-timeout-004";
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
  plan.allow_partial_results = true;
  plan.max_projection_items = 4U;
  plan.route_reason_codes = {"route_ok"};

  RecallRequest request;
  request.normalized_query = std::move(query);
  request.plan = std::move(plan);
  request.required_language = "zh-CN";
  return request;
}

[[nodiscard]] RecallRequest make_strict_request() {
  auto request = make_request();
  request.plan.allow_partial_results = false;
  return request;
}

void test_recall_coordinator_discards_late_dense_result_after_lane_timeout() {
  RecallCoordinator coordinator(
      RecallCoordinatorDeps{
          .sparse_lane = [](const SparseRetrieveRequest&) {
            SparseRetrieveResult result;
            result.ok = true;
            result.hits = {make_hit("chunk-sparse")};
            return result;
          },
          .dense_bridge = nullptr,
          .dense_lane = [](const DenseRecallRequest&) {
            std::this_thread::sleep_for(120ms);
            DenseRecallResult result;
            result.ok = true;
            result.hits = {make_hit("chunk-dense-late")};
            return result;
          },
      },
      RecallCoordinatorPolicy{
          .max_parallel_recall = 2U,
          .sparse_lane_timeout_ms = 40,
          .dense_lane_timeout_ms = 40,
      });

  const auto start = std::chrono::steady_clock::now();
  const auto result = coordinator.recall(make_request());
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start)
          .count();

  assert_true(result.ok,
              "hybrid recall should keep sparse success when dense lane times out");
  assert_true(result.has_consistent_values(),
              "timeout-driven degraded recall should preserve result shape");
  assert_true(result.candidates.degraded,
              "timed-out dense lane should mark the candidate set as degraded");
  assert_true(result.candidates.sparse_succeeded,
              "sparse lane should still succeed when dense lane times out");
  assert_true(!result.candidates.dense_succeeded,
              "late dense result should be discarded after timeout");
  assert_equal(1, static_cast<int>(result.candidates.sparse_hits.size()),
               "sparse hits should remain available after dense timeout");
  assert_equal(0, static_cast<int>(result.candidates.dense_hits.size()),
               "timed-out dense lane should not contribute late hits");
  assert_true(std::find(result.candidates.warnings.begin(),
                        result.candidates.warnings.end(),
                        "dense_recall_timeout") != result.candidates.warnings.end(),
              "dense timeout should surface as a stable degraded warning code");
  assert_true(elapsed < 100,
              "lane timeout should stop waiting for the late dense result");
}

void test_recall_coordinator_fails_when_parallel_lanes_both_time_out() {
  RecallCoordinator coordinator(
      RecallCoordinatorDeps{
          .sparse_lane = [](const SparseRetrieveRequest&) {
            std::this_thread::sleep_for(120ms);
            SparseRetrieveResult result;
            result.ok = true;
            result.hits = {make_hit("chunk-sparse-late")};
            return result;
          },
          .dense_bridge = nullptr,
          .dense_lane = [](const DenseRecallRequest&) {
            std::this_thread::sleep_for(120ms);
            DenseRecallResult result;
            result.ok = true;
            result.hits = {make_hit("chunk-dense-late")};
            return result;
          },
      },
      RecallCoordinatorPolicy{
          .max_parallel_recall = 2U,
          .sparse_lane_timeout_ms = 40,
          .dense_lane_timeout_ms = 40,
      });

  const auto start = std::chrono::steady_clock::now();
  const auto result = coordinator.recall(make_strict_request());
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start)
          .count();

  assert_true(!result.ok,
              "coordinator should fail when all requested lanes time out and partial results are disallowed");
  assert_true(result.has_consistent_values(),
              "dual-timeout failure should preserve the coordinator result shape");
  assert_equal(2, static_cast<int>(result.failure_reason_codes.size()),
               "dual-timeout failure should preserve both lane timeout reasons");
  assert_true(std::find(result.failure_reason_codes.begin(),
                        result.failure_reason_codes.end(),
                        "sparse_recall_timeout") != result.failure_reason_codes.end(),
              "sparse timeout should surface as a stable lane failure code");
  assert_true(std::find(result.failure_reason_codes.begin(),
                        result.failure_reason_codes.end(),
                        "dense_recall_timeout") != result.failure_reason_codes.end(),
              "dense timeout should surface as a stable lane failure code");
  assert_true(elapsed < 100,
              "parallel lane timeouts should fail fast instead of waiting for late completions");
}

}  // namespace

int main() {
  try {
    test_recall_coordinator_discards_late_dense_result_after_lane_timeout();
    test_recall_coordinator_fails_when_parallel_lanes_both_time_out();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}