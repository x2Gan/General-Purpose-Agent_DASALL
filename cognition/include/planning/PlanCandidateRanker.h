#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "plan/PlanGraph.h"

namespace dasall::cognition {
struct PlanningRequest;
}

namespace dasall::cognition::planning {

enum class PlanCandidateKind : std::uint8_t {
  Canonical = 0,
  LeanExecution = 1,
  DirectResponseFallback = 2,
  ClarificationFallback = 3,
};

struct PlanCandidate {
  plan::PlanGraph plan_graph;
  PlanCandidateKind candidate_kind = PlanCandidateKind::Canonical;
  float confidence = 0.0F;
  float budget_fit = 0.0F;
  float ranking_score = 0.0F;
};

struct RankedPlanCandidates {
  std::optional<PlanCandidate> primary_candidate;
  std::vector<PlanCandidate> backup_candidates;
  std::vector<PlanCandidate> ranked_candidates;
};

class PlanCandidateRanker final {
 public:
  [[nodiscard]] RankedPlanCandidates rank_candidates(
      const PlanningRequest& request,
      std::vector<PlanCandidate> candidates,
      std::size_t max_output_candidates = 3U) const;
};

}  // namespace dasall::cognition::planning