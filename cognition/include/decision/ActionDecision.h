#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dasall::cognition::decision {

// Schema baseline: cognition.reasoning.v1.

enum class ActionDecisionKind : std::uint8_t {
  NoDecision = 0,
  ExecuteAction = 1,
  DirectResponse = 2,
  AskClarification = 3,
  ConvergeSafe = 4,
};

struct ToolIntentHint {
  std::string tool_name;
  std::string intent_summary;
  std::vector<std::string> argument_hints;
  std::vector<std::string> evidence_refs;
};

struct DelegateHint {
  std::string delegate_target;
  std::string rationale;
  float confidence = 0.0F;
};

struct ResponseOutline {
  std::string summary;
  std::vector<std::string> key_points;
};

struct CandidateDecisionScore {
  std::string candidate_name;
  float score = 0.0F;
  std::optional<std::string> rationale;
};

struct ActionDecision {
  ActionDecisionKind decision_kind = ActionDecisionKind::NoDecision;
  std::optional<std::string> selected_node_id;
  std::optional<std::string> rationale;
  float confidence = 0.0F;
  bool clarification_needed = false;
  std::optional<std::string> clarification_question;
  std::optional<ToolIntentHint> tool_intent_hint;
  std::optional<DelegateHint> delegate_hint;
  std::optional<ResponseOutline> response_outline;
  std::vector<CandidateDecisionScore> candidate_scores;
  std::vector<std::string> diagnostics;
};

}  // namespace dasall::cognition::decision