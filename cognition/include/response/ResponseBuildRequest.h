#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "agent/BeliefState.h"
#include "agent/GoalContract.h"
#include "context/ContextPacket.h"
#include "decision/ActionDecision.h"
#include "observation/Observation.h"

namespace dasall::cognition {

struct ResponseBuildHints {
  bool prefer_template = false;
  bool allow_template_fallback = true;
  std::uint32_t max_summary_chars = 0;
  std::vector<std::string> required_sections;
};

struct ResponseBuildRequest {
  std::string caller_domain;
  std::string request_id;
  std::string trace_id;
  std::string profile_id;
  contracts::GoalContract goal_contract;
  contracts::ContextPacket context_packet;
  std::optional<contracts::BeliefState> belief_state;
  std::optional<contracts::Observation> latest_observation;
  std::optional<decision::ActionDecision> terminal_decision;
  ResponseBuildHints build_hints;
};

}  // namespace dasall::cognition
