#pragma once

#include <optional>
#include <string>
#include <vector>

#include "decision/ActionDecision.h"
#include "error/ErrorInfo.h"
#include "validation/StructuredPayloadView.h"

namespace dasall::cognition::projection {

struct ActionDecisionProjectionResult {
  bool ok = false;
  std::optional<decision::ActionDecision> action_decision;
  std::optional<contracts::ErrorInfo> error_info;
  std::vector<std::string> diagnostics;
};

class ActionDecisionStructuredProjector {
 public:
  [[nodiscard]] ActionDecisionProjectionResult project_action_decision(
      const validation::StructuredPayloadView& payload_view) const;
};

}  // namespace dasall::cognition::projection