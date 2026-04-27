#pragma once

#include "IReasoner.h"
#include "reasoning/DecisionProjector.h"

namespace dasall::cognition::reasoning {

class Reasoner final : public IReasoner {
 public:
  explicit Reasoner(CognitionConfig config = {});

  [[nodiscard]] decision::ActionDecision decide(
      const ReasoningRequest& request) override;

 private:
  [[nodiscard]] std::vector<decision::CandidateDecisionScore> score_candidates(
      const ReasoningRequest& request) const;
  [[nodiscard]] bool evaluate_clarification_need(
      const ReasoningRequest& request) const;
  [[nodiscard]] bool has_conflicting_observation(
      const ReasoningRequest& request) const;
  [[nodiscard]] bool is_near_budget_limit(
      const ReasoningRequest& request) const;
  [[nodiscard]] bool prefer_direct_response(
      const ReasoningRequest& request) const;
  [[nodiscard]] std::optional<plan::PlanNode> resolve_active_node(
      const ReasoningRequest& request) const;
  [[nodiscard]] std::string derive_clarification_question(
      const ReasoningRequest& request) const;
  [[nodiscard]] decision::ActionDecision validate_decision_thresholds(
      const ReasoningRequest& request,
      decision::ActionDecision decision) const;
  [[nodiscard]] float lookup_score(
      const std::vector<decision::CandidateDecisionScore>& candidate_scores,
      std::string_view candidate_name) const;

  CognitionConfig config_;
  DecisionProjector projector_;
};

}  // namespace dasall::cognition::reasoning