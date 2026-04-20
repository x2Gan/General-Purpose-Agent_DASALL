#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "config/MemoryConfig.h"
#include "store/StoreResult.h"
#include "vector/IEmbeddingAdapter.h"

namespace dasall::memory {

struct VectorDocument {
  std::string doc_id;
  std::string doc_type;
  std::string text;
  std::vector<float> embedding;
};

struct VectorHit {
  std::string doc_id;
  std::string doc_type;
  float score = 0.0F;
  std::string text_snippet;
};

struct VectorIndexHealth {
  bool available = false;
  int indexed_doc_count = 0;
  std::int64_t last_rebuild_at = 0;
  std::string backend_type;
};

class VectorMemoryIndexAdapter {
 public:
  virtual ~VectorMemoryIndexAdapter() = default;

  [[nodiscard]] virtual bool is_available() const = 0;
  [[nodiscard]] virtual StoreResult upsert(const VectorDocument& doc) = 0;
  [[nodiscard]] virtual std::vector<VectorHit> search(
      const std::string& query_text, int top_k) const = 0;
  [[nodiscard]] virtual VectorIndexHealth health() const = 0;
  [[nodiscard]] virtual StoreResult rebuild_index() = 0;
};

}  // namespace dasall::memory