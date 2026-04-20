#pragma once

#include "vector/VectorMemoryIndexAdapter.h"

namespace dasall::memory {

class UnavailableVectorMemoryIndexAdapter final : public VectorMemoryIndexAdapter {
 public:
  explicit UnavailableVectorMemoryIndexAdapter(
      const VectorConfig& config,
      // Non-owning; lifetime must be managed by the caller.
                                               IEmbeddingAdapter* embedding_adapter = nullptr);

  [[nodiscard]] bool is_available() const override;
  [[nodiscard]] StoreResult upsert(const VectorDocument& doc) override;
  [[nodiscard]] std::vector<VectorHit> search(
      const std::string& query_text, int top_k) const override;
  [[nodiscard]] VectorIndexHealth health() const override;
  [[nodiscard]] StoreResult rebuild_index() override;

 private:
  VectorConfig config_{};
  // Non-owning pointer; caller retains ownership.
  IEmbeddingAdapter* embedding_adapter_ = nullptr;
  VectorIndexHealth health_{};
};

}  // namespace dasall::memory