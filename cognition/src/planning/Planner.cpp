#include "planning/Planner.h"

#include <utility>

namespace dasall::cognition::planning {

Planner::Planner(CognitionConfig config) : builder_(std::move(config)) {}

plan::PlanGraph Planner::build_plan(const PlanningRequest& request) {
  return builder_.build_plan_graph(request);
}

plan::ReplanResult Planner::replan(const ReplanRequest& request) {
  return builder_.build_replan_graph(request);
}

}  // namespace dasall::cognition::planning