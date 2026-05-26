#include "retrieve/VectorRetrieverBridge.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>

namespace dasall::knowledge::retrieve {

namespace {

[[nodiscard]] bool has_unique_values(const std::vector<std::string>& values) {
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (std::find(values.begin() + static_cast<std::ptrdiff_t>(index + 1),
                  values.end(), values[index]) != values.end()) {
      return false;
    }
  }

  return true;
}

}  // namespace

bool DenseQueryRequest::has_consistent_values() const {
  return !query_text.empty() && top_k > 0U && has_unique_values(allowed_corpus_ids) &&
         has_unique_values(required_tags) &&
         (!required_language.has_value() || !required_language->empty());
}

bool DenseRecallRequest::has_consistent_values() const {
  return normalized_query.has_consistent_values() && plan.has_consistent_values() &&
         plan.mode != RetrievalMode::LexicalOnly && plan.dense_top_k > 0U &&
         (!required_language.has_value() || !required_language->empty());
}

bool DenseRecallResult::has_consistent_values() const {
  if (!has_unique_values(warnings) || !has_unique_values(failure_reason_codes)) {
    return false;
  }

  if (ok) {
    return std::all_of(hits.begin(), hits.end(), [](const RecallHit& hit) {
             return hit.has_consistent_values();
           }) &&
           failure_reason_codes.empty();
  }

  return hits.empty() && !failure_reason_codes.empty();
}

VectorRetrieverBridge::VectorRetrieverBridge(std::unique_ptr<IQueryEncoder> query_encoder,
                                             std::unique_ptr<IVectorRecallStore> vector_store)
    : query_encoder_(std::move(query_encoder)),
      vector_store_(std::move(vector_store)) {}

bool VectorRetrieverBridge::available() const {
  if (!vector_store_ || !vector_store_->available()) {
    return false;
  }

  if (vector_store_->query_input_mode() == DenseQueryInputMode::EmbeddingRequired) {
    return query_encoder_ && query_encoder_->available();
  }

  return true;
}

DenseRecallResult VectorRetrieverBridge::retrieve(const DenseRecallRequest& request) const {
  if (!request.has_consistent_values()) {
    return make_failure({"request_inconsistent"});
  }

  if (!vector_store_ || !vector_store_->available()) {
    return make_failure({"vector_backend_unavailable"});
  }

  const auto input_mode = vector_store_->query_input_mode();
  if (input_mode == DenseQueryInputMode::EmbeddingRequired &&
      (!query_encoder_ || !query_encoder_->available())) {
    return make_failure({"vector_backend_unavailable"});
  }

  auto dense_query = build_dense_query(request);
  if (!dense_query.has_consistent_values()) {
    return make_failure({"request_inconsistent"});
  }

  if (input_mode == DenseQueryInputMode::EmbeddingRequired &&
      dense_query.query_embedding.empty()) {
    return make_failure({"vector_backend_unavailable"},
                        {"query_encoder_empty_embedding"});
  }

  const auto hits = vector_store_->search(dense_query);
  if (!std::all_of(hits.begin(), hits.end(), [](const RecallHit& hit) {
        return hit.has_consistent_values();
      })) {
    return make_failure({"result_inconsistent"});
  }

  DenseRecallResult result;
  result.ok = true;
  result.hits = hits;
  return result;
}

DenseQueryRequest VectorRetrieverBridge::build_dense_query(
    const DenseRecallRequest& request) const {
  DenseQueryRequest dense_query;
  dense_query.query_text = request.normalized_query.normalized_text;
  dense_query.allowed_corpus_ids = request.plan.corpus_ids;
  dense_query.required_tags = request.normalized_query.required_tags;
  dense_query.required_language = request.required_language;
  dense_query.top_k = request.plan.dense_top_k;

  if (vector_store_ &&
      vector_store_->query_input_mode() == DenseQueryInputMode::EmbeddingRequired) {
    if (query_encoder_ && query_encoder_->available()) {
      dense_query.query_embedding = query_encoder_->encode(
          request.normalized_query.normalized_text);
    }
  }

  return dense_query;
}

DenseRecallResult VectorRetrieverBridge::make_failure(
    std::vector<std::string> failure_reason_codes,
    std::vector<std::string> warnings) const {
  DenseRecallResult result;
  result.ok = false;
  result.warnings = std::move(warnings);
  result.failure_reason_codes = std::move(failure_reason_codes);
  return result;
}

}  // namespace dasall::knowledge::retrieve