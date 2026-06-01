#include "reasoning/Reasoner.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>

namespace dasall::cognition::reasoning {
namespace {

constexpr std::string_view kExecuteCandidate = "execute_action";
constexpr std::string_view kDirectResponseCandidate = "direct_response";
constexpr std::string_view kClarificationCandidate = "ask_clarification";
constexpr std::string_view kConvergeSafeCandidate = "converge_safe";

[[nodiscard]] float clamp_score(float value) {
  return std::clamp(value, 0.0F, 0.95F);
}

[[nodiscard]] float apply_candidate_weight(float score, float weight) {
  return clamp_score(score * std::max(weight, 0.0F));
}

[[nodiscard]] bool text_has_conflict_signal(std::string_view payload) {
  return payload.find("contradict") != std::string_view::npos ||
         payload.find("incomplete") != std::string_view::npos ||
         payload.find("missing") != std::string_view::npos ||
         payload.find("conflict") != std::string_view::npos;
}

[[nodiscard]] bool has_high_budget_pressure(const ReasoningRequest& request) {
  return request.budget_context.has_value() &&
         request.budget_context->budget_utilization >= 0.8F;
}

void append_unique_diagnostic(std::vector<std::string>& diagnostics,
                              std::string diagnostic) {
  if (diagnostic.empty()) {
    return;
  }

  if (std::find(diagnostics.begin(), diagnostics.end(), diagnostic) ==
      diagnostics.end()) {
    diagnostics.push_back(std::move(diagnostic));
  }
}

void append_budget_pressure_decision_path(
    decision::ActionDecision& action_decision,
    std::string_view decision_path) {
  append_unique_diagnostic(action_decision.diagnostics,
                           std::string("budget_pressure_decision_path:") +
                               std::string(decision_path));
}

}  // namespace

Reasoner::Reasoner(CognitionConfig config)
    : config_(config), projector_(std::move(config)) {}

decision::ActionDecision Reasoner::decide(const ReasoningRequest& request) {
  auto candidate_scores = score_candidates(request);
  const auto execute_score = lookup_score(candidate_scores, kExecuteCandidate);
  const auto direct_response_score =
      lookup_score(candidate_scores, kDirectResponseCandidate);
  const auto clarification_score =
      lookup_score(candidate_scores, kClarificationCandidate);
  const auto converge_safe_score =
      lookup_score(candidate_scores, kConvergeSafeCandidate);

  const auto active_node = resolve_active_node(request);
  const auto clarification_needed = evaluate_clarification_need(request);
  const auto conflicting_observation = has_conflicting_observation(request);
  const auto near_budget_limit = is_near_budget_limit(request);
  const auto high_budget_pressure = has_high_budget_pressure(request);
  const auto prefer_direct = prefer_direct_response(request);
  std::optional<std::string_view> budget_pressure_decision_path;

  decision::ActionDecision decision;
  if ((clarification_needed || conflicting_observation) &&
      clarification_score >= config_.thresholds.ask_clarification) {
    decision = projector_.build_clarification_decision(
        request, derive_clarification_question(request), clarification_score,
        std::move(candidate_scores));
  } else if (high_budget_pressure) {
    const auto can_direct_respond =
        prefer_direct &&
        direct_response_score >= config_.thresholds.direct_response &&
        direct_response_score >= converge_safe_score;
    if (can_direct_respond) {
      decision = projector_.build_direct_response_decision(
          request, direct_response_score, std::move(candidate_scores));
      budget_pressure_decision_path = kDirectResponseCandidate;
    } else {
      decision = projector_.build_converge_safe_decision(
          request,
          "budget utilization reached the high-pressure threshold; prefer safe convergence over executing additional steps",
          std::max(converge_safe_score, config_.thresholds.replan_hint),
          std::move(candidate_scores));
      budget_pressure_decision_path = kConvergeSafeCandidate;
    }
  } else if (near_budget_limit &&
             converge_safe_score >= std::max(execute_score, direct_response_score) &&
             converge_safe_score >= config_.thresholds.replan_hint) {
    decision = projector_.build_converge_safe_decision(
        request,
        "budget pressure or exhausted actionable nodes require safe convergence",
        converge_safe_score, std::move(candidate_scores));
  } else if (prefer_direct &&
             direct_response_score >= config_.thresholds.direct_response &&
             direct_response_score >= execute_score) {
    decision = projector_.build_direct_response_decision(
        request, direct_response_score, std::move(candidate_scores));
  } else if (active_node.has_value() &&
             execute_score >= std::max(config_.thresholds.ask_clarification, 0.50F)) {
    decision = projector_.build_execute_action_decision(
        request, *active_node, execute_score, std::move(candidate_scores));
  } else if (clarification_score >= 0.40F) {
    decision = projector_.build_clarification_decision(
        request, derive_clarification_question(request), clarification_score,
        std::move(candidate_scores));
  } else if (converge_safe_score >= config_.thresholds.replan_hint) {
    decision = projector_.build_converge_safe_decision(
        request, "no strong actionable candidate remained after threshold validation",
        converge_safe_score, std::move(candidate_scores));
  } else if (active_node.has_value()) {
    decision = projector_.build_execute_action_decision(
        request, *active_node, execute_score, std::move(candidate_scores));
  } else {
    decision = projector_.build_direct_response_decision(
        request, direct_response_score, std::move(candidate_scores));
  }

  decision = validate_decision_thresholds(request, std::move(decision));
  if (budget_pressure_decision_path.has_value()) {
    append_budget_pressure_decision_path(decision,
                                         *budget_pressure_decision_path);
  }
  return decision;
}

std::vector<decision::CandidateDecisionScore> Reasoner::score_candidates(
    const ReasoningRequest& request) const {
  const auto active_node = resolve_active_node(request);
  const auto clarification_needed = evaluate_clarification_need(request);
  const auto conflicting_observation = has_conflicting_observation(request);
  const auto near_budget_limit = is_near_budget_limit(request);
  const auto prefer_direct = prefer_direct_response(request);

  const auto belief_confidence = request.belief_state.confidence.value_or(0.50F);
  const auto perception_confidence = request.perception_result.confidence;

  float execute_score = active_node.has_value()
                            ? 0.30F + perception_confidence * 0.35F +
                                  belief_confidence * 0.20F
                            : 0.0F;
  if (clarification_needed) {
    execute_score -= 0.25F;
  }
  if (conflicting_observation) {
    execute_score -= 0.45F;
  }
  if (near_budget_limit) {
    execute_score -= 0.15F;
  }
  execute_score = apply_candidate_weight(execute_score,
                                         config_.reasoner.candidate_weights.tool_call);

  float direct_response_score = 0.10F;
  if (prefer_direct) {
    direct_response_score += 0.55F;
    if (request.active_plan.nodes.size() == 1U) {
      direct_response_score += 0.15F;
    }
  }
  if (near_budget_limit) {
    direct_response_score += 0.15F;
  }
  if (request.latest_observation.has_value() &&
      request.latest_observation->success.value_or(false)) {
    direct_response_score += 0.10F;
  }
  if (clarification_needed) {
    direct_response_score -= 0.20F;
  }
  if (conflicting_observation) {
    direct_response_score -= 0.15F;
  }
  direct_response_score = apply_candidate_weight(
      direct_response_score, config_.reasoner.candidate_weights.direct_response);

  float clarification_score = 0.12F;
  if (clarification_needed) {
    clarification_score += 0.45F;
  }
  if (conflicting_observation) {
    clarification_score += 0.35F;
  }
  if (request.budget_context.has_value() &&
      request.budget_context->context_was_truncated) {
    clarification_score += 0.05F;
  }
  clarification_score = apply_candidate_weight(
      clarification_score, config_.reasoner.candidate_weights.clarification);

  float converge_safe_score = 0.10F;
  if (near_budget_limit) {
    converge_safe_score += 0.40F;
  }
  if (!active_node.has_value()) {
    converge_safe_score += 0.20F;
  }
  if (request.latest_observation.has_value() &&
      !request.latest_observation->success.value_or(true)) {
    converge_safe_score += 0.10F;
  }
  converge_safe_score = apply_candidate_weight(
      converge_safe_score, config_.reasoner.candidate_weights.converge_safe);

  return {
      decision::CandidateDecisionScore{.candidate_name = std::string(kExecuteCandidate),
                                       .score = execute_score,
                                       .rationale = std::string(
                                           "active node execution viability")},
      decision::CandidateDecisionScore{.candidate_name =
                                           std::string(kDirectResponseCandidate),
                                       .score = direct_response_score,
                                       .rationale = std::string(
                                           "direct response suitability")},
      decision::CandidateDecisionScore{.candidate_name =
                                           std::string(kClarificationCandidate),
                                       .score = clarification_score,
                                       .rationale = std::string(
                                           "clarification need severity")},
      decision::CandidateDecisionScore{.candidate_name =
                                           std::string(kConvergeSafeCandidate),
                                       .score = converge_safe_score,
                                       .rationale = std::string(
                                           "safe convergence preference")},
  };
}

bool Reasoner::evaluate_clarification_need(const ReasoningRequest& request) const {
  return request.perception_result.requires_clarification ||
         !request.active_plan.open_questions.empty() ||
         request.perception_result.confidence < config_.thresholds.ask_clarification;
}

bool Reasoner::has_conflicting_observation(const ReasoningRequest& request) const {
  if (!request.latest_observation.has_value()) {
    return false;
  }

  if (!request.latest_observation->success.value_or(true)) {
    return true;
  }

  if (request.latest_observation->payload.has_value() &&
      text_has_conflict_signal(*request.latest_observation->payload)) {
    return true;
  }

  if (request.latest_observation->error.has_value() &&
      !request.latest_observation->error->details.message.empty() &&
      text_has_conflict_signal(request.latest_observation->error->details.message)) {
    return true;
  }

  return false;
}

bool Reasoner::is_near_budget_limit(const ReasoningRequest& request) const {
  return request.budget_context.has_value() &&
         (request.budget_context->near_budget_limit ||
          request.budget_context->budget_utilization >= 0.8F);
}

bool Reasoner::prefer_direct_response(const ReasoningRequest& request) const {
  return request.perception_result.task_type == "direct_response" ||
         (request.active_plan.nodes.size() == 1U &&
          request.active_plan.nodes.front().action_kind_hint == "direct_response");
}

std::optional<plan::PlanNode> Reasoner::resolve_active_node(
    const ReasoningRequest& request) const {
  for (const auto& node : request.active_plan.nodes) {
    if (node.action_kind_hint != "validation" &&
        node.action_kind_hint != "direct_response") {
      return node;
    }
  }

  return std::nullopt;
}

std::string Reasoner::derive_clarification_question(
    const ReasoningRequest& request) const {
  if (!request.active_plan.open_questions.empty()) {
    return request.active_plan.open_questions.front().question;
  }

  if (!request.perception_result.clarification_questions.empty()) {
    return request.perception_result.clarification_questions.front().question;
  }

  if (has_conflicting_observation(request)) {
    return std::string(
        "The latest observation conflicts with the active plan. What detail should be corrected before continuing?");
  }

  return std::string(
      "What additional detail should the agent clarify before continuing this task?");
}

decision::ActionDecision Reasoner::validate_decision_thresholds(
    const ReasoningRequest& request,
    decision::ActionDecision decision) const {
  switch (decision.decision_kind) {
    case decision::ActionDecisionKind::ExecuteAction:
      if (!decision.selected_node_id.has_value() ||
          !decision.tool_intent_hint.has_value()) {
        return projector_.build_clarification_decision(
            request, derive_clarification_question(request),
            config_.thresholds.ask_clarification, std::move(decision.candidate_scores));
      }
      return decision;
    case decision::ActionDecisionKind::DirectResponse:
      if (decision.confidence < config_.thresholds.direct_response) {
        decision.decision_kind = decision::ActionDecisionKind::ConvergeSafe;
        decision.rationale =
            std::string("direct response confidence fell below the direct response threshold");
      }
      return decision;
    case decision::ActionDecisionKind::AskClarification:
      decision.clarification_needed = true;
      if (!decision.clarification_question.has_value()) {
        decision.clarification_question = derive_clarification_question(request);
      }
      return decision;
    case decision::ActionDecisionKind::ConvergeSafe:
      if (!decision.response_outline.has_value()) {
        return projector_.build_converge_safe_decision(
            request, "safe convergence retained after response outline validation",
            std::max(decision.confidence, config_.thresholds.replan_hint),
            std::move(decision.candidate_scores));
      }
      return decision;
    case decision::ActionDecisionKind::NoDecision:
      break;
  }

  return projector_.build_converge_safe_decision(
      request, "no valid decision candidate remained after threshold validation",
      config_.thresholds.replan_hint, std::move(decision.candidate_scores));
}

float Reasoner::lookup_score(
    const std::vector<decision::CandidateDecisionScore>& candidate_scores,
    std::string_view candidate_name) const {
  for (const auto& candidate : candidate_scores) {
    if (candidate.candidate_name == candidate_name) {
      return candidate.score;
    }
  }

  return 0.0F;
}

}  // namespace dasall::cognition::reasoning