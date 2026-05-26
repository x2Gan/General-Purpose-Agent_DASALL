#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "retrieve/VectorRetrieverBridge.h"
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
using dasall::knowledge::retrieve::IQueryEncoder;
using dasall::knowledge::retrieve::IVectorRecallStore;
using dasall::knowledge::retrieve::RecallHit;
using dasall::knowledge::retrieve::VectorRetrieverBridge;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class StaticQueryEncoder final : public IQueryEncoder {
 public:
  explicit StaticQueryEncoder(std::vector<float> embedding)
      : embedding_(std::move(embedding)) {}

  [[nodiscard]] std::vector<float> encode(std::string_view query_text) const override {
    last_query_text_ = std::string(query_text);
    ++encode_calls_;
    return embedding_;
  }

  [[nodiscard]] bool available() const override {
    return true;
  }

  mutable std::string last_query_text_;
  mutable int encode_calls_ = 0;

 private:
  std::vector<float> embedding_;
};

class EmptyEmbeddingQueryEncoder final : public IQueryEncoder {
 public:
  [[nodiscard]] std::vector<float> encode(std::string_view query_text) const override {
    last_query_text_ = std::string(query_text);
    ++encode_calls_;
    return {};
  }

  [[nodiscard]] bool available() const override {
    return true;
  }

  mutable std::string last_query_text_;
  mutable int encode_calls_ = 0;
};

class RecordingVectorStore final : public IVectorRecallStore {
 public:
  explicit RecordingVectorStore(DenseQueryInputMode mode)
      : mode_(mode) {}

  [[nodiscard]] bool available() const override {
    return true;
  }

  [[nodiscard]] DenseQueryInputMode query_input_mode() const override {
    return mode_;
  }

  [[nodiscard]] std::vector<RecallHit> search(const DenseQueryRequest& request) const override {
    last_request_ = request;
    return {make_hit(mode_ == DenseQueryInputMode::TextOnly ? "chunk-text" : "chunk-embedding")};
  }

  mutable std::optional<DenseQueryRequest> last_request_;

 private:
  static RecallHit make_hit(std::string chunk_id) {
    RecallHit hit;
    hit.corpus_id = "adr_normative";
    hit.document_id = "adr-001";
    hit.chunk_id = std::move(chunk_id);
    hit.score = 0.72F;
    hit.raw_snippet = "dense recall snippet";
    hit.citation_ref = "ADR-001#dense";
    hit.updated_at = 1713657600000;
    hit.authority_level = AuthorityLevel::Normative;
    hit.tags = {"normative", "policy"};
    return hit;
  }

  DenseQueryInputMode mode_;
};

[[nodiscard]] DenseRecallRequest make_request() {
  NormalizedQuery query;
  query.request_id = "req-dense-001";
  query.normalized_text = "policy evidence";
  query.lexical_terms = {"policy", "evidence"};
  query.domain_tags = {"normative", "policy"};
  query.allowed_corpora = {"adr_normative", "architecture_reference"};
  query.required_tags = {"normative", "policy"};
  query.query_kind = KnowledgeQueryKind::PolicyEvidence;
  query.top_k = 5U;
  query.max_context_projection_items = 4U;

  RetrievalPlan plan;
  plan.mode = RetrievalMode::Hybrid;
  plan.corpus_ids = {"adr_normative", "architecture_reference"};
  plan.sparse_top_k = 5U;
  plan.dense_top_k = 3U;
  plan.allow_partial_results = true;
  plan.max_projection_items = 4U;
  plan.route_reason_codes = {"route_ok"};

  DenseRecallRequest request;
  request.normalized_query = std::move(query);
  request.plan = std::move(plan);
  request.required_language = "zh-CN";
  return request;
}

void test_vector_retriever_bridge_passes_text_only_request_without_encoder() {
  auto store = std::make_unique<RecordingVectorStore>(DenseQueryInputMode::TextOnly);
  auto* store_ptr = store.get();
  VectorRetrieverBridge bridge(nullptr, std::move(store));

  assert_true(bridge.available(),
              "text-only vector store should make the bridge available without an encoder");

  const auto result = bridge.retrieve(make_request());
  assert_true(result.ok,
              "text-only dense retrieval should succeed when vector store is available");
  assert_true(result.has_consistent_values(),
              "successful text-only dense retrieval should keep a consistent result shape");
  assert_equal(1, static_cast<int>(result.hits.size()),
               "text-only dense retrieval should return the store hits");
  assert_true(store_ptr->last_request_.has_value(),
              "text-only dense retrieval should pass a query request to the store");
  assert_equal(std::string("policy evidence"), store_ptr->last_request_->query_text,
               "text-only dense retrieval should pass normalized text through unchanged");
  assert_true(store_ptr->last_request_->query_embedding.empty(),
              "text-only dense retrieval must not require query embeddings");
  assert_equal(2, static_cast<int>(store_ptr->last_request_->allowed_corpus_ids.size()),
               "text-only dense retrieval should preserve routed corpus filters");
  assert_equal(2, static_cast<int>(store_ptr->last_request_->required_tags.size()),
               "text-only dense retrieval should preserve domain tag filters");
}

void test_vector_retriever_bridge_builds_embedding_when_backend_requires_it() {
  auto encoder = std::make_unique<StaticQueryEncoder>(std::vector<float>{0.1F, 0.3F, 0.6F});
  auto* encoder_ptr = encoder.get();
  auto store = std::make_unique<RecordingVectorStore>(DenseQueryInputMode::EmbeddingRequired);
  auto* store_ptr = store.get();
  VectorRetrieverBridge bridge(std::move(encoder), std::move(store));

  assert_true(bridge.available(),
              "embedding-required vector store should be available when encoder is injected");

  const auto result = bridge.retrieve(make_request());
  assert_true(result.ok,
              "embedding-required dense retrieval should succeed when encoder and store are available");
  assert_true(result.has_consistent_values(),
              "embedding-required dense retrieval should preserve a consistent result shape");
  assert_equal(1, encoder_ptr->encode_calls_,
               "embedding-required dense retrieval should encode the query exactly once");
  assert_equal(std::string("policy evidence"), encoder_ptr->last_query_text_,
               "encoder should receive the normalized query text");
  assert_true(store_ptr->last_request_.has_value(),
              "embedding-required dense retrieval should pass a request to the store");
  assert_equal(3, static_cast<int>(store_ptr->last_request_->query_embedding.size()),
               "embedding-required dense retrieval should attach the encoded embedding");
  assert_equal(3, static_cast<int>(store_ptr->last_request_->top_k),
               "dense request should preserve the routed dense top-k budget");
}

void test_vector_retriever_bridge_reports_empty_encoder_output_as_unavailable() {
  auto encoder = std::make_unique<EmptyEmbeddingQueryEncoder>();
  auto* encoder_ptr = encoder.get();
  auto store = std::make_unique<RecordingVectorStore>(DenseQueryInputMode::EmbeddingRequired);
  VectorRetrieverBridge bridge(std::move(encoder), std::move(store));

  const auto result = bridge.retrieve(make_request());
  assert_true(!result.ok,
              "embedding-required dense retrieval should fail when encoder returns an empty embedding");
  assert_true(result.has_consistent_values(),
              "empty-embedding failure should keep a consistent result shape");
  assert_equal(1, encoder_ptr->encode_calls_,
               "embedding-required dense retrieval should still call the encoder once before failing");
  assert_equal(std::string("policy evidence"), encoder_ptr->last_query_text_,
               "empty-embedding failure should still pass normalized query text to the encoder");
  assert_true(result.failure_reason_codes.size() == 1U &&
                  result.failure_reason_codes.front() == "vector_backend_unavailable",
              "empty-embedding failure should keep the stable unavailable reason code");
  assert_true(result.warnings.size() == 1U &&
                  result.warnings.front() == "query_encoder_empty_embedding",
              "empty-embedding failure should expose a diagnostic warning without changing the failure class");
}

}  // namespace

int main() {
  try {
    test_vector_retriever_bridge_passes_text_only_request_without_encoder();
    test_vector_retriever_bridge_builds_embedding_when_backend_requires_it();
    test_vector_retriever_bridge_reports_empty_encoder_output_as_unavailable();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}