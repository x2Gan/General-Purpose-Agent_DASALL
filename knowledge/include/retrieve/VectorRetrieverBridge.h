#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "query/CorpusRouter.h"
#include "query/QueryNormalizer.h"
#include "retrieve/IQueryEncoder.h"
#include "retrieve/IVectorRecallStore.h"

namespace dasall::knowledge::retrieve {

struct DenseRecallRequest {
  query::NormalizedQuery normalized_query;
  query::RetrievalPlan plan;
  std::optional<std::string> required_language;

  [[nodiscard]] bool has_consistent_values() const;
};

struct DenseRecallResult {
  bool ok = false;
  std::vector<RecallHit> hits;
  std::vector<std::string> warnings;
  std::vector<std::string> failure_reason_codes;

  [[nodiscard]] bool has_consistent_values() const;
};

class VectorRetrieverBridge {
 public:
  VectorRetrieverBridge(std::unique_ptr<IQueryEncoder> query_encoder,
                        std::unique_ptr<IVectorRecallStore> vector_store);

  VectorRetrieverBridge(const VectorRetrieverBridge&) = delete;
  VectorRetrieverBridge& operator=(const VectorRetrieverBridge&) = delete;
  VectorRetrieverBridge(VectorRetrieverBridge&&) noexcept = default;
  VectorRetrieverBridge& operator=(VectorRetrieverBridge&&) noexcept = default;
  ~VectorRetrieverBridge() = default;

  [[nodiscard]] bool available() const;
  [[nodiscard]] DenseRecallResult retrieve(const DenseRecallRequest& request) const;

 private:
  [[nodiscard]] DenseQueryRequest build_dense_query(const DenseRecallRequest& request) const;
  [[nodiscard]] DenseRecallResult make_failure(
      std::vector<std::string> failure_reason_codes,
      std::vector<std::string> warnings = {}) const;

  std::unique_ptr<IQueryEncoder> query_encoder_;
  std::unique_ptr<IVectorRecallStore> vector_store_;
};

}  // namespace dasall::knowledge::retrieve