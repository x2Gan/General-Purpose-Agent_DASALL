#pragma once

#include <cstdint>
#include <vector>

#include "CognitionConfig.h"
#include "IPlanner.h"
#include "planning/PlanCandidateRanker.h"

namespace dasall::cognition::planning {

struct PlanBuildLimits {
  std::uint32_t max_plan_nodes = 1U;
  std::uint32_t max_plan_depth = 1U;
  bool degraded_mode = false;
};

class PlanGraphBuilder {
 public:
  explicit PlanGraphBuilder(CognitionConfig config);

  [[nodiscard]] plan::PlanGraph build_plan_graph(const PlanningRequest& request) const;
  [[nodiscard]] std::vector<PlanCandidate> build_plan_candidates(
      const PlanningRequest& request) const;
  [[nodiscard]] plan::ReplanResult build_replan_graph(const ReplanRequest& request) const;

 private:
  [[nodiscard]] PlanBuildLimits derive_limits(
      const std::optional<BudgetContext>& budget_context) const;
  [[nodiscard]] plan::PlanGraph build_clarification_plan(
      const PlanningRequest& request,
      const PlanBuildLimits& limits) const;
  [[nodiscard]] plan::PlanGraph build_direct_response_plan(
      const PlanningRequest& request,
      const PlanBuildLimits& limits) const;
  [[nodiscard]] plan::PlanGraph build_actionable_plan(
      const PlanningRequest& request,
      const PlanBuildLimits& limits) const;
  [[nodiscard]] std::vector<plan::PlanNode> expand_goal_into_nodes(
      const PlanningRequest& request) const;
  [[nodiscard]] plan::PlanGraph compress_plan_when_budget_tight(
      plan::PlanGraph graph,
      const PlanBuildLimits& limits) const;
  [[nodiscard]] bool validate_plan_graph(const plan::PlanGraph& graph,
                                         const PlanBuildLimits& limits) const;

  CognitionConfig config_;
};

}  // namespace dasall::cognition::planning