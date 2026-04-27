#pragma once

#include <optional>
#include <string>

#include "CognitionTypes.h"
#include "perception/PerceptionResult.h"
#include "plan/PlanGraph.h"

namespace dasall::cognition {

struct ReasoningRequest {
  std::string caller_domain;
  std::string request_id;
  std::string trace_id;
  std::string profile_id;
  contracts::GoalContract goal_contract;
  contracts::ContextPacket context_packet;
  contracts::BeliefState belief_state;
  perception::PerceptionResult perception_result;
  plan::PlanGraph active_plan;
  std::optional<contracts::Observation> latest_observation;
  std::optional<BudgetContext> budget_context;
  StageExecutionHints execution_hints;
};

class IReasoner {
 public:
  virtual ~IReasoner() = default;

  [[nodiscard]] virtual decision::ActionDecision decide(
      const ReasoningRequest& request) = 0;

 protected:
  IReasoner() = default;
  IReasoner(const IReasoner&) = default;
  IReasoner& operator=(const IReasoner&) = default;
  IReasoner(IReasoner&&) = default;
  IReasoner& operator=(IReasoner&&) = default;
};

}  // namespace dasall::cognition
