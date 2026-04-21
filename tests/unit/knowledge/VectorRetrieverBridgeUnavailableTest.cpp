#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "retrieve/VectorRetrieverBridge.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::KnowledgeQueryKind;
using dasall::knowledge::RetrievalMode;
using dasall::knowledge::query::NormalizedQuery;
using dasall::knowledge::query::RetrievalPlan;
using dasall::knowledge::retrieve::DenseQueryInputMode;
using dasall::knowledge::retrieve::DenseQueryRequest;
using dasall::knowledge::retrieve::DenseRecallRequest;
using dasall::knowledge::retrieve::IQueryEncoder;
using dasall::knowledge::retrieve::IVectorRecallStore;
using dasall::knowledge::retrieve::RecallHit;
using dasall::knowledge::retrieve::VectorRetrieverBridge;
using dasall::tests::support::assert_true;

class UnavailableVectorStore final : public IVectorRecallStore {
 public:
  explicit UnavailableVectorStore(DenseQueryInputMode mode)
      : mode_(mode) {}

  [[nodiscard]] bool available() const override {
    return false;
  }

  [[nodiscard]] DenseQueryInputMode query_input_mode() const override {
    return mode_;
  }

  [[nodiscard]] std::vector<RecallHit> search(const DenseQueryRequest&) const override {
    return {};
  }

 private:
  DenseQueryInputMode mode_;
};

class UnavailableQueryEncoder final : public IQueryEncoder {
 public:
  [[nodiscard]] std::vector<float> encode(std::string_view) const override {
    return {};
  }

  [[nodiscard]] bool available() const override {
    return false;
  }
};

class EmbeddingRequiredStore final : public IVectorRecallStore {
 public:
  [[nodiscard]] bool available() const override {
    return true;
  }

  [[nodiscard]] DenseQueryInputMode query_input_mode() const override {
    return DenseQueryInputMode::EmbeddingRequired;
  }

  [[nodiscard]] std::vector<RecallHit> search(const DenseQueryRequest&) const override {
    return {};
  }
};

[[nodiscard]] DenseRecallRequest make_request() {
  NormalizedQuery query;
  query.request_id = "req-dense-002";
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
  plan.dense_top_k = 2U;
  plan.allow_partial_results = true;
  plan.max_projection_items = 4U;
  plan.route_reason_codes = {"route_ok"};

  DenseRecallRequest request;
  request.normalized_query = std::move(query);
  request.plan = std::move(plan);
  request.required_language = "zh-CN";
  return request;
}

void test_vector_retriever_bridge_reports_unavailable_backend() {
  VectorRetrieverBridge bridge(
      nullptr,
      std::make_unique<UnavailableVectorStore>(DenseQueryInputMode::TextOnly));

  assert_true(!bridge.available(),
              "bridge should report unavailable when vector store is unavailable");

  const auto result = bridge.retrieve(make_request());
  assert_true(!result.ok,
              "dense retrieval should fail at lane level when backend is unavailable");
  assert_true(result.has_consistent_values(),
              "backend-unavailable dense retrieval should keep a consistent failure shape");
  assert_true(result.failure_reason_codes.size() == 1U &&
                  result.failure_reason_codes.front() == "vector_backend_unavailable",
              "backend-unavailable dense retrieval should expose the stable unavailable reason code");
}

void test_vector_retriever_bridge_reports_missing_required_encoder_as_unavailable() {
  VectorRetrieverBridge bridge(
      std::make_unique<UnavailableQueryEncoder>(),
      std::make_unique<EmbeddingRequiredStore>());

  assert_true(!bridge.available(),
              "embedding-required bridge should be unavailable when encoder is unavailable");

  const auto result = bridge.retrieve(make_request());
  assert_true(!result.ok,
              "embedding-required dense retrieval should fail at lane level without a usable encoder");
  assert_true(result.has_consistent_values(),
              "missing-encoder dense retrieval should keep a consistent failure shape");
  assert_true(result.failure_reason_codes.size() == 1U &&
                  result.failure_reason_codes.front() == "vector_backend_unavailable",
              "missing-encoder dense retrieval should collapse to the stable unavailable reason code");
}

}  // namespace

int main() {
  try {
    test_vector_retriever_bridge_reports_unavailable_backend();
    test_vector_retriever_bridge_reports_missing_required_encoder_as_unavailable();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}