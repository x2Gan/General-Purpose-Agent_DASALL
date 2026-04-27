#pragma once

#include "IPlanner.h"
#include "planning/PlanGraphBuilder.h"

namespace dasall::cognition::planning {

class Planner final : public IPlanner {
 public:
  explicit Planner(CognitionConfig config = {});

  [[nodiscard]] plan::PlanGraph build_plan(
      const PlanningRequest& request) override;
  [[nodiscard]] plan::ReplanResult replan(
      const ReplanRequest& request) override;

 private:
  PlanGraphBuilder builder_;
};

}  // namespace dasall::cognition::planning