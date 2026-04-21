#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "KnowledgeErrors.h"
#include "KnowledgeTypes.h"
#include "health/FreshnessController.h"
#include "index/CorpusCatalog.h"
#include "query/QueryNormalizer.h"

namespace dasall::knowledge::query {

struct RetrievalPlan {
  RetrievalMode mode = RetrievalMode::LexicalOnly;
  std::vector<std::string> corpus_ids;
  std::size_t sparse_top_k = 0U;
  std::size_t dense_top_k = 0U;
  bool allow_partial_results = false;
  bool allow_stale_snapshot = false;
  std::size_t max_projection_items = 0U;
  std::vector<std::string> route_reason_codes;

  [[nodiscard]] bool has_consistent_values() const;
};

struct RoutePlanResult {
  bool ok = false;
  std::optional<RetrievalPlan> plan;
  std::vector<std::string> route_reason_codes;
  std::optional<dasall::contracts::ErrorInfo> error;

  [[nodiscard]] bool has_consistent_values() const;
};

class CorpusRouter {
 public:
  [[nodiscard]] RoutePlanResult build_plan(const NormalizedQuery& query,
                                           const KnowledgeConfigSnapshot& config,
                                           const index::CorpusCatalogSnapshot& catalog,
                                           const FreshnessSnapshot& freshness) const;

 private:
  [[nodiscard]] RetrievalMode select_mode(const NormalizedQuery& query,
                                          const KnowledgeConfigSnapshot& config,
                                          const std::vector<CorpusDescriptor>& candidates) const;
};

}  // namespace dasall::knowledge::query