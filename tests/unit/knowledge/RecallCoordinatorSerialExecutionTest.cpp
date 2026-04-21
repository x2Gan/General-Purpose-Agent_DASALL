#include <exception>
#include <iostream>
#include <string>
#include <vector>

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

[[nodiscard]] RecallHit make_hit(std::string chunk_id) {
  RecallHit hit;
  hit.corpus_id = "adr_normative";
  hit.document_id = "adr-001";
  hit.chunk_id = std::move(chunk_id);
  hit.score = 0.5F;
  hit.raw_snippet = "policy evidence snippet";
  hit.citation_ref = "ADR-001#policy";
  hit.updated_at = 1713657600000;
  hit.authority_level = AuthorityLevel::Normative;
  hit.tags = {"normative"};
  return hit;
}

[[nodiscard]] RecallRequest make_request() {
  NormalizedQuery query;
  query.request_id = "req-003";
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

void test_recall_coordinator_keeps_v1_hybrid_execution_serial() {
  std::vector<std::string> call_order;
  RecallCoordinator coordinator(
      RecallCoordinatorDeps{
          .sparse_lane = [&call_order](const SparseRetrieveRequest&) {
            call_order.push_back("sparse");
            SparseRetrieveResult result;
            result.ok = true;
            result.hits = {make_hit("chunk-sparse")};
            return result;
          },
          .dense_lane = [&call_order](const DenseRecallRequest&) {
            call_order.push_back("dense");
            DenseRecallResult result;
            result.ok = true;
            result.hits = {make_hit("chunk-dense")};
            return result;
          },
      },
      RecallCoordinatorPolicy{
          .max_parallel_recall = 4U,
          .sparse_lane_timeout_ms = 100,
          .dense_lane_timeout_ms = 100,
      });

  const auto result = coordinator.recall(make_request());
  assert_true(result.ok,
              "serial v1 recall should still succeed when both lanes succeed");
  assert_equal(2, static_cast<int>(call_order.size()),
               "hybrid recall should execute exactly two lanes in v1");
  assert_equal("sparse", call_order.front(),
               "v1 hybrid recall should always execute sparse lane first");
  assert_equal("dense", call_order.back(),
               "v1 hybrid recall should execute dense lane second even when parallel budget is available");
}

}  // namespace

int main() {
  try {
    test_recall_coordinator_keeps_v1_hybrid_execution_serial();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}