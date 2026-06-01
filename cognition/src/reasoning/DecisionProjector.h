#pragma once

#include <optional>
#include <string>
#include <vector>

#include "CognitionConfig.h"
#include "IReasoner.h"

namespace dasall::cognition::reasoning {

class DecisionProjector {
 public:
  explicit DecisionProjector(CognitionConfig config);

  [[nodiscard]] decision::ActionDecision build_execute_action_decision(
      const ReasoningRequest& request,
      const plan::PlanNode& active_node,
      float confidence,
      std::vector<decision::CandidateDecisionScore> candidate_scores) const;

  [[nodiscard]] decision::ActionDecision build_direct_response_decision(
      const ReasoningRequest& request,
      float confidence,
      std::vector<decision::CandidateDecisionScore> candidate_scores) const;

  [[nodiscard]] decision::ActionDecision build_clarification_decision(
      const ReasoningRequest& request,
      std::string clarification_question,
      float confidence,
      std::vector<decision::CandidateDecisionScore> candidate_scores) const;

  [[nodiscard]] decision::ActionDecision build_converge_safe_decision(
      const ReasoningRequest& request,
      std::string rationale,
      float confidence,
      std::vector<decision::CandidateDecisionScore> candidate_scores) const;

 private:
    struct ToolIntentProjection {
        std::optional<decision::ToolIntentHint> hint;
        std::vector<std::string> diagnostics;
    };

  [[nodiscard]] decision::ResponseOutline project_response_outline(
      const ReasoningRequest& request,
      std::string_view mode,
      std::string_view focus) const;

    [[nodiscard]] ToolIntentProjection build_tool_intent_hint(
      const ReasoningRequest& request,
      const plan::PlanNode& active_node) const;

  CognitionConfig config_;
};

}  // namespace dasall::cognition::reasoning