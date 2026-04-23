#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dasall::cognition::decision {

enum class ActionDecisionKind : std::uint8_t {
  NoDecision = 0,
  ExecuteAction = 1,
  DirectResponse = 2,
  AskClarification = 3,
  ConvergeSafe = 4,
};

struct ActionDecision {
  ActionDecisionKind decision_kind = ActionDecisionKind::NoDecision;
  float confidence = 0.0F;
  std::optional<std::string> rationale;
  std::optional<std::string> tool_name;
  std::optional<std::string> tool_arguments_payload;
  std::optional<std::string> response_text;
  std::optional<std::string> clarification_question;
  std::optional<std::vector<std::string>> evidence_refs;
};

}  // namespace dasall::cognition::decision