#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "retrieve/RecallTypes.h"
#include "retrieve/SparseRetriever.h"
#include "retrieve/VectorRetrieverBridge.h"

namespace dasall::knowledge::retrieve {

struct RecallRequest {
  query::NormalizedQuery normalized_query;
  query::RetrievalPlan plan;
  std::optional<std::string> required_language;

  [[nodiscard]] bool has_consistent_values() const;
};

struct RecallCoordinatorResult {
  bool ok = false;
  RecallCandidateSet candidates;
  std::vector<std::string> failure_reason_codes;

  [[nodiscard]] bool has_consistent_values() const;
};

struct RecallCoordinatorDeps {
  std::function<SparseRetrieveResult(const SparseRetrieveRequest& request)> sparse_lane;
  std::function<DenseRecallResult(const DenseRecallRequest& request)> dense_lane;
};

struct RecallCoordinatorPolicy {
  std::size_t max_parallel_recall = 1U;
  std::int64_t sparse_lane_timeout_ms = 1;
  std::int64_t dense_lane_timeout_ms = 1;

  [[nodiscard]] bool has_consistent_values() const;
};

class RecallCoordinator {
 public:
  explicit RecallCoordinator(RecallCoordinatorDeps deps,
                             RecallCoordinatorPolicy policy = {});

  [[nodiscard]] RecallCoordinatorResult recall(const RecallRequest& request) const;

 private:
  [[nodiscard]] SparseRetrieveResult run_sparse_lane(const RecallRequest& request) const;
  [[nodiscard]] DenseRecallResult run_dense_lane(const RecallRequest& request) const;

  RecallCoordinatorDeps deps_{};
  RecallCoordinatorPolicy policy_{};
};

}  // namespace dasall::knowledge::retrieve