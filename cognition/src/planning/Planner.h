#pragma once

#include "IPlanner.h"
#include "planning/PlanCandidateRanker.h"
#include "planning/PlanGraphBuilder.h"

namespace dasall::cognition::planning {

class Planner final : public IPlanner {
 public:
  explicit Planner(CognitionConfig config = {});

  [[nodiscard]] plan::PlanGraph build_plan(
      const PlanningRequest& request) override;
  [[nodiscard]] RankedPlanCandidates build_ranked_plan_candidates(
      const PlanningRequest& request) const;
  [[nodiscard]] plan::ReplanResult replan(
      const ReplanRequest& request) override;

 private:
  [[nodiscard]] std::size_t derive_candidate_limit(
      const std::optional<BudgetContext>& budget_context) const;

  PlanGraphBuilder builder_;
  PlanCandidateRanker ranker_;
};

}  // namespace dasall::cognition::planning