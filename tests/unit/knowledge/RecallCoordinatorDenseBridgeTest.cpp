#include <exception>
#include <iostream>
#include <memory>
#include <optional>
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
using dasall::knowledge::retrieve::DenseQueryInputMode;
using dasall::knowledge::retrieve::DenseQueryRequest;
using dasall::knowledge::retrieve::DenseRecallRequest;
using dasall::knowledge::retrieve::DenseRecallResult;
using dasall::knowledge::retrieve::IQueryEncoder;
using dasall::knowledge::retrieve::IVectorRecallStore;
using dasall::knowledge::retrieve::RecallCoordinator;
using dasall::knowledge::retrieve::RecallCoordinatorDeps;
using dasall::knowledge::retrieve::RecallHit;
using dasall::knowledge::retrieve::RecallRequest;
using dasall::knowledge::retrieve::SparseRetrieveRequest;
using dasall::knowledge::retrieve::SparseRetrieveResult;
using dasall::knowledge::retrieve::VectorRetrieverBridge;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class TextOnlyVectorStore final : public IVectorRecallStore {
 public:
  [[nodiscard]] bool available() const override {
    return true;
  }

  [[nodiscard]] DenseQueryInputMode query_input_mode() const override {
    return DenseQueryInputMode::TextOnly;
  }

  [[nodiscard]] std::vector<RecallHit> search(const DenseQueryRequest& request) const override {
    last_request_ = request;

    RecallHit hit;
    hit.corpus_id = "adr_normative";
    hit.document_id = "adr-001";
    hit.chunk_id = "chunk-dense-bridge";
    hit.score = 0.83F;
    hit.raw_snippet = "dense bridge snippet";
    hit.citation_ref = "ADR-001#dense-bridge";
    hit.updated_at = 1713657600000;
    hit.authority_level = AuthorityLevel::Normative;
    hit.tags = {"normative"};
    return {hit};
  }

  mutable std::optional<DenseQueryRequest> last_request_;
};

class UnusedEncoder final : public IQueryEncoder {
 public:
  [[nodiscard]] std::vector<float> encode(std::string_view) const override {
    return {};
  }

  [[nodiscard]] bool available() const override {
    return false;
  }
};

[[nodiscard]] RecallHit make_sparse_hit() {
  RecallHit hit;
  hit.corpus_id = "adr_normative";
  hit.document_id = "adr-001";
  hit.chunk_id = "chunk-sparse";
  hit.score = 0.61F;
  hit.raw_snippet = "sparse snippet";
  hit.citation_ref = "ADR-001#sparse";
  hit.updated_at = 1713657600000;
  hit.authority_level = AuthorityLevel::Normative;
  hit.tags = {"normative"};
  return hit;
}

[[nodiscard]] RecallRequest make_request() {
  NormalizedQuery query;
  query.request_id = "req-bridge-priority-014";
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

void test_recall_coordinator_prefers_real_dense_bridge_over_fallback_seam() {
  auto vector_store = std::make_unique<TextOnlyVectorStore>();
  auto* vector_store_ptr = vector_store.get();
  auto dense_bridge = std::make_shared<VectorRetrieverBridge>(
      std::make_unique<UnusedEncoder>(),
      std::move(vector_store));
  bool fallback_dense_called = false;

  RecallCoordinator coordinator(RecallCoordinatorDeps{
      .sparse_lane = [](const SparseRetrieveRequest&) {
        SparseRetrieveResult result;
        result.ok = true;
        result.hits = {make_sparse_hit()};
        return result;
      },
      .dense_bridge = dense_bridge,
      .dense_lane = [&fallback_dense_called](const DenseRecallRequest&) {
        fallback_dense_called = true;
        DenseRecallResult result;
        result.ok = false;
        result.failure_reason_codes = {"unexpected_fallback_dense_lane"};
        return result;
      },
  });

  const auto result = coordinator.recall(make_request());
  assert_true(result.ok,
              "coordinator should succeed when sparse lane and real dense bridge both succeed");
  assert_true(result.has_consistent_values(),
              "coordinator result should stay consistent when dense bridge is wired in");
  assert_true(result.candidates.sparse_succeeded,
              "real dense bridge path should keep sparse success");
  assert_true(result.candidates.dense_succeeded,
              "real dense bridge path should mark dense lane as succeeded");
  assert_true(!result.candidates.degraded,
              "real dense bridge success should not degrade the candidate set");
  assert_true(!fallback_dense_called,
              "real dense bridge should take precedence over fallback dense seam");
  assert_true(vector_store_ptr->last_request_.has_value(),
              "coordinator should forward a dense request to the real bridge-backed store");
  assert_equal(std::string("policy evidence"), vector_store_ptr->last_request_->query_text,
               "real dense bridge should preserve normalized text into the vector store request");
  assert_equal(1, static_cast<int>(result.candidates.dense_hits.size()),
               "real dense bridge should contribute dense hits into the candidate set");
}

}  // namespace

int main() {
  try {
    test_recall_coordinator_prefers_real_dense_bridge_over_fallback_seam();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}