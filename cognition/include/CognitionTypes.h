#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "agent/AgentResult.h"
#include "agent/BeliefState.h"
#include "context/ContextPacket.h"
#include "observation/Observation.h"

#include "belief/BeliefUpdateHint.h"
#include "decision/ActionDecision.h"

namespace dasall::cognition {

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
  std::string request_id;
  std::string trace_id;
  std::string profile_id;
  std::optional<std::string> goal_id;
  contracts::ContextPacket context_packet;
  contracts::BeliefState belief_state;
  std::optional<contracts::Observation> latest_observation;
  std::optional<BudgetContext> budget_context;
};

struct CognitionDecisionResult {
  decision::ActionDecision action_decision;
  std::optional<belief::BeliefUpdateHint> belief_update_hint;
  ContextSufficiencySignal context_sufficiency;
  std::vector<std::string> diagnostics;
};

struct ReflectionRequest {
  std::string request_id;
  std::string trace_id;
  std::string profile_id;
  std::optional<std::string> goal_id;
  contracts::ContextPacket context_packet;
  contracts::BeliefState belief_state;
  contracts::Observation latest_observation;
};

struct CognitionReflectionResult {
  decision::ActionDecision action_decision;
  std::optional<belief::BeliefUpdateHint> belief_update_hint;
  std::vector<std::string> diagnostics;
};

struct ResponseBuildRequest {
  std::string request_id;
  std::string trace_id;
  std::string profile_id;
  std::optional<std::string> goal_id;
  contracts::ContextPacket context_packet;
  std::optional<contracts::Observation> latest_observation;
  std::optional<decision::ActionDecision> action_decision;
};

struct ResponseBuildResult {
  contracts::AgentResult agent_result;
  bool fallback_used = false;
  std::vector<std::string> diagnostics;
};

}  // namespace dasall::cognition