#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "agent/BeliefState.h"
#include "agent/GoalContract.h"
#include "checkpoint/ReflectionDecision.h"
#include "context/ContextPacket.h"
#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "observation/Observation.h"

#include "belief/BeliefUpdateHint.h"
#include "decision/ActionDecision.h"
#include "response/ResponseBuildRequest.h"
#include "response/ResponseBuildResult.h"

namespace dasall::cognition {

struct StageExecutionHints {
  bool low_latency_preferred = false;
  bool degraded_path_allowed = true;
  float risk_tolerance = 0.0F;
  std::optional<std::string> profile_variant_hint;
};

struct BudgetContext {
  std::uint32_t total_budget_tokens = 0;
  std::uint32_t consumed_tokens = 0;
  std::uint32_t remaining_tokens = 0;
  float budget_utilization = 0.0F;
  bool context_was_truncated = false;
  bool near_budget_limit = false;
};

struct ContextSufficiencySignal {
  bool context_sufficient = true;
  float context_confidence = 1.0F;
  std::vector<std::string> missing_evidence_hints;
  bool recommend_context_reload = false;
};

struct CognitionStepRequest {
  std::string caller_domain;
  std::string request_id;
  std::string trace_id;
  std::string profile_id;
  contracts::GoalContract goal_contract;
  contracts::ContextPacket context_packet;
  contracts::BeliefState belief_state;
  std::optional<contracts::Observation> latest_observation;
  std::optional<BudgetContext> budget_context;
  StageExecutionHints execution_hints;
};

struct CognitionDecisionResult {
  std::optional<contracts::ResultCode> result_code;
  std::optional<decision::ActionDecision> action_decision;
  std::optional<belief::BeliefUpdateHint> belief_update_hint;
  std::optional<contracts::ErrorInfo> error_info;
  ContextSufficiencySignal context_sufficiency;
  std::vector<std::string> diagnostics;
};

struct ReflectionRequest {
  std::string caller_domain;
  std::string request_id;
  std::string trace_id;
  std::string profile_id;
  contracts::GoalContract goal_contract;
  contracts::ContextPacket context_packet;
  contracts::BeliefState belief_state;
  contracts::Observation latest_observation;
  std::optional<std::string> active_plan_ref;
  StageExecutionHints execution_hints;
};

struct CognitionReflectionResult {
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ReflectionDecision> reflection_decision;
  std::optional<belief::BeliefUpdateHint> belief_update_hint;
  std::optional<contracts::ErrorInfo> error_info;
  std::vector<std::string> diagnostics;
};

}  // namespace dasall::cognition
