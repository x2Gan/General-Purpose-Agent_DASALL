#include "reasoning/DecisionProjector.h"

#include <algorithm>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace dasall::cognition::reasoning {
namespace {

template <typename T>
[[nodiscard]] const std::vector<T>& optional_vector_or_empty(
    const std::optional<std::vector<T>>& value) {
  static const std::vector<T> kEmpty;
  return value.has_value() ? *value : kEmpty;
}

[[nodiscard]] std::string optional_string_or(const std::optional<std::string>& value,
                                             std::string_view fallback) {
  return value.has_value() ? *value : std::string(fallback);
}

void append_unique(std::vector<std::string>& target,
                   const std::vector<std::string>& source) {
  std::unordered_set<std::string> seen(target.begin(), target.end());
  for (const auto& value : source) {
    if (!value.empty() && seen.insert(value).second) {
      target.push_back(value);
    }
  }
}

[[nodiscard]] std::string first_tool_name(const ReasoningRequest& request) {
  for (const auto& entity : request.perception_result.entities) {
    if (entity.name == "tool" && !entity.value.empty()) {
      return entity.value;
    }
  }

  const auto& active_tools = optional_vector_or_empty(request.context_packet.active_tools);
  return active_tools.empty() ? std::string("planner.next_step") : active_tools.front();
}

}  // namespace

DecisionProjector::DecisionProjector(CognitionConfig config)
    : config_(std::move(config)) {}

decision::ActionDecision DecisionProjector::build_execute_action_decision(
    const ReasoningRequest& request,
    const plan::PlanNode& active_node,
    float confidence,
    std::vector<decision::CandidateDecisionScore> candidate_scores) const {
  return decision::ActionDecision{
      .decision_kind = decision::ActionDecisionKind::ExecuteAction,
      .selected_node_id = active_node.node_id,
      .rationale = std::string("highest-scoring actionable node selected for execution"),
      .confidence = confidence,
      .clarification_needed = false,
      .clarification_question = std::nullopt,
      .tool_intent_hint = build_tool_intent_hint(request, active_node),
      .delegate_hint = std::nullopt,
      .response_outline = project_response_outline(request, "execute", active_node.objective),
      .candidate_scores = std::move(candidate_scores),
  };
}

decision::ActionDecision DecisionProjector::build_direct_response_decision(
    const ReasoningRequest& request,
    float confidence,
    std::vector<decision::CandidateDecisionScore> candidate_scores) const {
  return decision::ActionDecision{
      .decision_kind = decision::ActionDecisionKind::DirectResponse,
      .selected_node_id = std::nullopt,
      .rationale = std::string("direct response scored above the response threshold"),
      .confidence = confidence,
      .clarification_needed = false,
      .clarification_question = std::nullopt,
      .tool_intent_hint = std::nullopt,
      .delegate_hint = std::nullopt,
      .response_outline = project_response_outline(
          request, "direct_response",
          optional_string_or(request.goal_contract.goal_description, "current goal")),
      .candidate_scores = std::move(candidate_scores),
  };
}

decision::ActionDecision DecisionProjector::build_clarification_decision(
    const ReasoningRequest& request,
    std::string clarification_question,
    float confidence,
    std::vector<decision::CandidateDecisionScore> candidate_scores) const {
  return decision::ActionDecision{
      .decision_kind = decision::ActionDecisionKind::AskClarification,
      .selected_node_id = std::nullopt,
      .rationale = std::string("clarification is required before safe execution can continue"),
      .confidence = confidence,
      .clarification_needed = true,
      .clarification_question = std::move(clarification_question),
      .tool_intent_hint = std::nullopt,
      .delegate_hint = std::nullopt,
      .response_outline = project_response_outline(
          request, "clarify",
          optional_string_or(request.goal_contract.goal_description, "current goal")),
      .candidate_scores = std::move(candidate_scores),
  };
}

decision::ActionDecision DecisionProjector::build_converge_safe_decision(
    const ReasoningRequest& request,
    std::string rationale,
    float confidence,
    std::vector<decision::CandidateDecisionScore> candidate_scores) const {
  return decision::ActionDecision{
      .decision_kind = decision::ActionDecisionKind::ConvergeSafe,
      .selected_node_id = std::nullopt,
      .rationale = std::move(rationale),
      .confidence = confidence,
      .clarification_needed = false,
      .clarification_question = std::nullopt,
      .tool_intent_hint = std::nullopt,
      .delegate_hint = std::nullopt,
      .response_outline = project_response_outline(
          request, "converge_safe",
          optional_string_or(request.goal_contract.success_criteria, "safe completion")),
      .candidate_scores = std::move(candidate_scores),
  };
}

decision::ResponseOutline DecisionProjector::project_response_outline(
    const ReasoningRequest& request,
    std::string_view mode,
    std::string_view focus) const {
  decision::ResponseOutline outline;
  outline.summary = std::string(mode) + ": " + std::string(focus);
  outline.key_points.push_back(optional_string_or(
      request.goal_contract.goal_description, "goal description unavailable"));
  outline.key_points.push_back(optional_string_or(
      request.goal_contract.success_criteria, "success criteria unavailable"));

  if (request.latest_observation.has_value() &&
      request.latest_observation->payload.has_value() &&
      !request.latest_observation->payload->empty()) {
    outline.key_points.push_back(*request.latest_observation->payload);
  }

  return outline;
}

std::optional<decision::ToolIntentHint> DecisionProjector::build_tool_intent_hint(
    const ReasoningRequest& request,
    const plan::PlanNode& active_node) const {
  decision::ToolIntentHint hint;
  hint.tool_name = first_tool_name(request);
  hint.intent_summary = active_node.objective;

  if (request.goal_contract.goal_description.has_value() &&
      !request.goal_contract.goal_description->empty()) {
    hint.argument_hints.push_back(*request.goal_contract.goal_description);
  }
  if (request.goal_contract.success_criteria.has_value() &&
      !request.goal_contract.success_criteria->empty()) {
    hint.argument_hints.push_back(*request.goal_contract.success_criteria);
  }

  append_unique(hint.evidence_refs, active_node.evidence_refs);
  append_unique(hint.evidence_refs,
                optional_vector_or_empty(request.belief_state.evidence_refs));
  return hint;
}

}  // namespace dasall::cognition::reasoning