#pragma once

#include <optional>
#include <string>

#include "CognitionTypes.h"
#include "perception/PerceptionResult.h"
#include "plan/ReplanResult.h"

namespace dasall::cognition {

struct PlanningRequest {
  std::string caller_domain;
  std::string request_id;
  std::string trace_id;
  std::string profile_id;
  contracts::GoalContract goal_contract;
  contracts::ContextPacket context_packet;
  contracts::BeliefState belief_state;
  perception::PerceptionResult perception_result;
  std::optional<BudgetContext> budget_context;
  StageExecutionHints execution_hints;
};

struct ReplanRequest {
  std::string caller_domain;
  std::string request_id;
  std::string trace_id;
  std::string profile_id;
  contracts::GoalContract goal_contract;
  contracts::ContextPacket context_packet;
  contracts::BeliefState belief_state;
  plan::PlanGraph active_plan;
  contracts::Observation latest_observation;
  std::optional<BudgetContext> budget_context;
  StageExecutionHints execution_hints;
};

class IPlanner {
 public:
  virtual ~IPlanner() = default;

  [[nodiscard]] virtual plan::PlanGraph build_plan(
      const PlanningRequest& request) = 0;
  [[nodiscard]] virtual plan::ReplanResult replan(
      const ReplanRequest& request) = 0;

 protected:
  IPlanner() = default;
  IPlanner(const IPlanner&) = default;
  IPlanner& operator=(const IPlanner&) = default;
  IPlanner(IPlanner&&) = default;
  IPlanner& operator=(IPlanner&&) = default;
};

}  // namespace dasall::cognition
