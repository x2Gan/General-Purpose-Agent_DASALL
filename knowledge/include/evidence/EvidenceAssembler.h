#pragma once

#include <cstddef>
#include <string>

#include "KnowledgeTypes.h"
#include "rerank/Reranker.h"

namespace dasall::knowledge::evidence {

struct EvidenceAssemblePolicy {
  std::size_t evidence_budget_tokens = 256U;
  std::size_t max_context_projection_items = 6U;
  float stale_confidence_penalty = 0.1F;
  std::size_t chars_per_token = 4U;
  float budget_safety_margin_ratio = 0.1F;

  [[nodiscard]] bool has_consistent_values() const;

  [[nodiscard]] static EvidenceAssemblePolicy from_query(const KnowledgeQuery& query,
                                                         const KnowledgeConfigSnapshot& config);
};

class EvidenceAssembler {
 public:
  [[nodiscard]] EvidenceBundle assemble(const rerank::RankedHitSet& hits,
                                        const EvidenceAssemblePolicy& policy) const;

 private:
  [[nodiscard]] std::string build_projection_line(const rerank::RankedHit& hit) const;
  [[nodiscard]] EvidenceSlice build_slice(const rerank::RankedHit& hit,
                                          float max_fused_score,
                                          float stale_confidence_penalty) const;
};

}  // namespace dasall::knowledge::evidence