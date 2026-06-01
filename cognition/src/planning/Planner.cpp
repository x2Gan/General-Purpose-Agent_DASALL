#include "planning/Planner.h"

#include <utility>

namespace dasall::cognition::planning {

Planner::Planner(CognitionConfig config) : builder_(std::move(config)) {}

std::size_t Planner::derive_candidate_limit(
    const std::optional<BudgetContext>& budget_context) const {
  if (!budget_context.has_value()) {
    return 3U;
  }

  if (budget_context->near_budget_limit || budget_context->budget_utilization >= 0.8F) {
    return 2U;
  }

  return 3U;
}

RankedPlanCandidates Planner::build_ranked_plan_candidates(
    const PlanningRequest& request) const {
  return ranker_.rank_candidates(request,
                                 builder_.build_plan_candidates(request),
                                 derive_candidate_limit(request.budget_context));
}

plan::PlanGraph Planner::build_plan(const PlanningRequest& request) {
  auto ranked_candidates = build_ranked_plan_candidates(request);
  if (ranked_candidates.primary_candidate.has_value()) {
    return ranked_candidates.primary_candidate->plan_graph;
  }

  return builder_.build_plan_graph(request);
}

plan::ReplanResult Planner::replan(const ReplanRequest& request) {
  return builder_.build_replan_graph(request);
}

}  // namespace dasall::cognition::planning