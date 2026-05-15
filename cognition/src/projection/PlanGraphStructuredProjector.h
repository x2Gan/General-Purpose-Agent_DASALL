#pragma once

#include <optional>
#include <string>
#include <vector>

#include "error/ErrorInfo.h"
#include "plan/PlanGraph.h"
#include "validation/StructuredPayloadView.h"

namespace dasall::cognition::projection {

struct PlanGraphProjectionResult {
  bool ok = false;
  std::optional<plan::PlanGraph> plan_graph;
  std::optional<contracts::ErrorInfo> error_info;
  std::vector<std::string> diagnostics;
};

class PlanGraphStructuredProjector {
 public:
  [[nodiscard]] PlanGraphProjectionResult project_plan_graph(
      const validation::StructuredPayloadView& payload_view) const;
};

}  // namespace dasall::cognition::projection