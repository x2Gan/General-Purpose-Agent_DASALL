#pragma once

// Schema baseline: cognition.plan.v1.

#include <string>
#include <vector>

#include "plan/PlanGraph.h"

namespace dasall::cognition::plan {

struct ReplanResult {
  PlanGraph new_plan;
  std::vector<std::string> replaced_node_ids;
  std::string replan_reason;
  float confidence = 0.0F;
};

}  // namespace dasall::cognition::plan
