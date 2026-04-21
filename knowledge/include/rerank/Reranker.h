#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "health/FreshnessController.h"
#include "retrieve/RecallTypes.h"

namespace dasall::knowledge::rerank {

struct RankedHit {
  retrieve::RecallHit hit;
  float fused_score = 0.0F;
  bool stale = false;
  std::vector<std::string> score_reason_codes;

  [[nodiscard]] bool has_consistent_values() const;
};

struct RankedHitSet {
  std::vector<RankedHit> hits;
  bool degraded = false;

  [[nodiscard]] bool has_consistent_values() const;
};

struct RerankPolicy {
  std::size_t top_k = 8U;
  std::size_t rrf_k = 60U;
  float stale_penalty_factor = 0.85F;
  float normative_authority_boost = 1.05F;
  float reference_authority_boost = 1.0F;
  float advisory_authority_boost = 0.95F;
  float min_score_cutoff = 0.0F;

  [[nodiscard]] bool has_consistent_values() const;
};

class Reranker {
 public:
  [[nodiscard]] RankedHitSet rerank(const retrieve::RecallCandidateSet& candidates,
                                    const FreshnessSnapshot& freshness,
                                    const RerankPolicy& policy) const;
};

}  // namespace dasall::knowledge::rerank