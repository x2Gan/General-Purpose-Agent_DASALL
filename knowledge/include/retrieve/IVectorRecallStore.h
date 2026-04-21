#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "retrieve/RecallTypes.h"

namespace dasall::knowledge::retrieve {

enum class DenseQueryInputMode {
  TextOnly = 0,
  EmbeddingRequired = 1,
};

struct DenseQueryRequest {
  std::string query_text;
  std::vector<float> query_embedding;
  std::vector<std::string> allowed_corpus_ids;
  std::vector<std::string> required_tags;
  std::optional<std::string> required_language;
  std::size_t top_k = 0U;

  [[nodiscard]] bool has_consistent_values() const;
};

class IVectorRecallStore {
 public:
  virtual ~IVectorRecallStore() = default;

  [[nodiscard]] virtual bool available() const = 0;
  [[nodiscard]] virtual DenseQueryInputMode query_input_mode() const = 0;
  [[nodiscard]] virtual std::vector<RecallHit> search(
      const DenseQueryRequest& request) const = 0;
};

}  // namespace dasall::knowledge::retrieve