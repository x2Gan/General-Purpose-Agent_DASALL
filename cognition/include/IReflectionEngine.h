#pragma once

#include <optional>
#include <string>

#include "CognitionTypes.h"
#include "plan/PlanGraph.h"

namespace dasall::cognition {

struct ReflectionAnalysisRequest {
  std::string caller_domain;
  std::string request_id;
  std::string trace_id;
  std::string profile_id;
  contracts::GoalContract goal_contract;
  contracts::BeliefState belief_state;
  contracts::Observation latest_observation;
  std::optional<contracts::ErrorInfo> error_info;
  std::optional<plan::PlanGraph> active_plan;
  StageExecutionHints execution_hints;
};

class IReflectionEngine {
 public:
  virtual ~IReflectionEngine() = default;

  [[nodiscard]] virtual contracts::ReflectionDecision analyze(
      const ReflectionAnalysisRequest& request) = 0;

 protected:
  IReflectionEngine() = default;
  IReflectionEngine(const IReflectionEngine&) = default;
  IReflectionEngine& operator=(const IReflectionEngine&) = default;
  IReflectionEngine(IReflectionEngine&&) = default;
  IReflectionEngine& operator=(IReflectionEngine&&) = default;
};

}  // namespace dasall::cognition
