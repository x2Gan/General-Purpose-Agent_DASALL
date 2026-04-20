#include "vector/VectorMemoryIndexAdapter.h"

#include <utility>

namespace dasall::memory {

namespace {

std::string resolve_backend_type(const VectorConfig& config) {
  if (!config.enabled) {
    return "none";
  }
  return std::string(to_string_view(config.backend_type));
}

}  // namespace

UnavailableVectorMemoryIndexAdapter::UnavailableVectorMemoryIndexAdapter(
    const VectorConfig& config,
    IEmbeddingAdapter* embedding_adapter)
    : config_(config),
      embedding_adapter_(embedding_adapter),
      health_{
          .available = false,
          .indexed_doc_count = 0,
          .last_rebuild_at = 0,
          .backend_type = resolve_backend_type(config),
      } {}

bool UnavailableVectorMemoryIndexAdapter::is_available() const {
  return false;
}

StoreResult UnavailableVectorMemoryIndexAdapter::upsert(const VectorDocument& doc) {
  (void)doc;
  return StoreResult::success();
}

std::vector<VectorHit> UnavailableVectorMemoryIndexAdapter::search(
    const std::string& query_text,
    int top_k) const {
  (void)query_text;
  (void)top_k;
  return {};
}

VectorIndexHealth UnavailableVectorMemoryIndexAdapter::health() const {
  return health_;
}

StoreResult UnavailableVectorMemoryIndexAdapter::rebuild_index() {
  return StoreResult::success();
}

}  // namespace dasall::memory