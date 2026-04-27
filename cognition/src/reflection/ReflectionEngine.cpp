#include "reflection/ReflectionEngine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <string_view>
#include <utility>

#include "checkpoint/ReflectionDecisionGuards.h"

namespace {

using dasall::contracts::ErrorInfo;
using dasall::contracts::ReflectionDecisionKind;
using dasall::contracts::ResultCodeCategory;
using dasall::cognition::ReflectionAnalysisRequest;

[[nodiscard]] std::string to_lower_copy(std::string_view value) {
  std::string lowered;
  lowered.reserve(value.size());
  for (const char character : value) {
    lowered.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(character))));
  }
  return lowered;
}

[[nodiscard]] bool contains_any(
    std::string_view haystack,
    std::initializer_list<std::string_view> needles) {
  for (const auto needle : needles) {
    if (haystack.find(needle) != std::string_view::npos) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::vector<std::string> extract_keywords(std::string_view value) {
  std::vector<std::string> keywords;
  std::string current;
  auto flush_current = [&]() {
    if (current.size() < 3U) {
      current.clear();
      return;
    }

    static constexpr std::string_view kStopWords[] = {
        "the",   "and",  "for",    "with",   "that", "this",
        "from",  "into", "have",   "has",    "was",  "were",
        "will",  "would", "should", "true",   "false", "user",
        "goal",  "step", "plan",   "current", "must", "need",
    };

    for (const auto stop_word : kStopWords) {
      if (current == stop_word) {
        current.clear();
        return;
      }
    }

    if (std::find(keywords.begin(), keywords.end(), current) == keywords.end()) {
      keywords.push_back(current);
    }
    current.clear();
  };

  for (const char raw_character : value) {
    const auto character = static_cast<char>(
        std::tolower(static_cast<unsigned char>(raw_character)));
    if (std::isalnum(static_cast<unsigned char>(character))) {
      current.push_back(character);
    } else {
      flush_current();
    }
  }
  flush_current();
  return keywords;
}

[[nodiscard]] std::string join_strings(
    const std::vector<std::string>& values,
    std::string_view separator) {
  std::string joined;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0U) {
      joined.append(separator);
    }
    joined.append(values[index]);
  }
  return joined;
}

[[nodiscard]] const ErrorInfo* resolve_error_info(
    const ReflectionAnalysisRequest& request) {
  if (request.error_info.has_value()) {
    return &*request.error_info;
  }
  if (request.latest_observation.error.has_value()) {
    return &*request.latest_observation.error;
  }
  return nullptr;
}

[[nodiscard]] std::string collect_failure_text(
    const ReflectionAnalysisRequest& request) {
  std::string combined;

  if (request.latest_observation.payload.has_value()) {
    combined.append(*request.latest_observation.payload);
    combined.push_back(' ');
  }

  if (const auto* error_info = resolve_error_info(request); error_info != nullptr) {
    combined.append(error_info->details.message);
    combined.push_back(' ');
    combined.append(error_info->details.stage);
    combined.push_back(' ');
  }

  if (request.goal_contract.success_criteria.has_value()) {
    combined.append(*request.goal_contract.success_criteria);
    combined.push_back(' ');
  }

  if (request.active_plan.has_value()) {
    for (const auto& node : request.active_plan->nodes) {
      combined.append(node.objective);
      combined.push_back(' ');
      combined.append(node.success_signal);
      combined.push_back(' ');
    }
  }

  if (request.latest_observation.side_effects.has_value()) {
    for (const auto& side_effect : *request.latest_observation.side_effects) {
      combined.append(side_effect);
      combined.push_back(' ');
    }
  }

  return to_lower_copy(combined);
}

[[nodiscard]] float clamp01(float value) {
  return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] std::string decision_tag(ReflectionDecisionKind decision_kind) {
  switch (decision_kind) {
    case ReflectionDecisionKind::Continue:
      return "continue";
    case ReflectionDecisionKind::RetryStep:
      return "retry_step";
    case ReflectionDecisionKind::Replan:
      return "replan";
    case ReflectionDecisionKind::AbortSafe:
      return "abort_safe";
    case ReflectionDecisionKind::Unspecified:
      break;
  }

  return "unspecified";
}

[[nodiscard]] std::optional<std::string> first_active_node_id(
    const ReflectionAnalysisRequest& request) {
  if (!request.active_plan.has_value() || request.active_plan->nodes.empty()) {
    return std::nullopt;
  }
  return request.active_plan->nodes.front().node_id;
}

}  // namespace

namespace dasall::cognition::reflection {

ReflectionEngine::ReflectionEngine(CognitionConfig config)
    : config_(std::move(config)) {}

contracts::ReflectionDecision ReflectionEngine::analyze(
  const ReflectionAnalysisRequest& request) {
  const auto failure_assessment = classify_failure_source(request);
  const auto belief_invalidations = detect_assumption_invalidations(request);
  const auto goal_gap_assessment =
      evaluate_goal_gap(request, failure_assessment, belief_invalidations);

  return validate_reflection_contract(
      request,
      project_reflection_decision(
          request, failure_assessment, goal_gap_assessment, belief_invalidations));
}

ReflectionEngine::FailureAssessment ReflectionEngine::classify_failure_source(
    const ReflectionAnalysisRequest& request) const {
  FailureAssessment assessment;
  const auto evidence = collect_failure_text(request);
  const auto success = request.latest_observation.success.value_or(false);
  const auto* error_info = resolve_error_info(request);

  assessment.failure_source = success ? "observation_success" : "unknown_failure";
  assessment.recoverability_score = success ? 0.95F : 0.30F;
  assessment.safety_risk = success ? 0.05F : 0.55F;

  if (error_info != nullptr) {
    assessment.retryable = error_info->retryable.value_or(false);
    assessment.safe_to_replan = error_info->safe_to_replan.value_or(false);
  }

  if (success) {
    return assessment;
  }

  const auto has_side_effects = request.latest_observation.side_effects.has_value() &&
                                !request.latest_observation.side_effects->empty();
  if (has_side_effects ||
      contains_any(evidence,
                   {"manual intervention", "irreversible", "committed",
                    "side effect", "unsafe"})) {
    assessment.failure_source = "unsafe_failure";
    assessment.retryable = false;
    assessment.safe_to_replan = false;
    assessment.recoverability_score = 0.12F;
    assessment.safety_risk = 0.90F;
    return assessment;
  }

  if (assessment.retryable ||
      contains_any(evidence,
                   {"timeout", "temporarily unavailable", "rate limit",
                    "transient", "retry later"})) {
    assessment.failure_source = "transient_execution_failure";
    assessment.retryable = true;
    assessment.safe_to_replan = true;
    assessment.recoverability_score = 0.82F;
    assessment.safety_risk = 0.24F;
    return assessment;
  }

  if ((error_info != nullptr &&
       error_info->failure_type == ResultCodeCategory::Policy) ||
      contains_any(evidence,
                   {"permission denied", "policy denied", "forbidden",
                    "not authorized"})) {
    assessment.failure_source = "policy_failure";
    assessment.retryable = false;
    assessment.safe_to_replan = false;
    assessment.recoverability_score = 0.18F;
    assessment.safety_risk = 0.78F;
    return assessment;
  }

  if (contains_any(evidence,
                   {"not available", "unavailable", "schema changed",
                    "blocked", "missing prerequisite", "mismatch",
                    "conflict"})) {
    assessment.failure_source = "assumption_or_environment_shift";
    assessment.safe_to_replan = true;
    assessment.recoverability_score = 0.72F;
    assessment.safety_risk = 0.40F;
    return assessment;
  }

  if (error_info != nullptr && !error_info->retryable.value_or(false) &&
      !error_info->safe_to_replan.value_or(false)) {
    assessment.failure_source = "non_recoverable_failure";
    assessment.recoverability_score = 0.15F;
    assessment.safety_risk = 0.76F;
    return assessment;
  }

  assessment.failure_source = "ambiguous_failure";
  assessment.recoverability_score = 0.22F;
  assessment.safety_risk = 0.60F;
  return assessment;
}

ReflectionEngine::GoalGapAssessment ReflectionEngine::evaluate_goal_gap(
    const ReflectionAnalysisRequest& request,
    const FailureAssessment& failure_assessment,
    const std::vector<std::string>& belief_invalidations) const {
  GoalGapAssessment assessment;
  const auto evidence = collect_failure_text(request);

  assessment.local_step_failure =
      !request.latest_observation.success.value_or(false);
  assessment.goal_gap =
      !belief_invalidations.empty() ||
      contains_any(evidence,
                   {"incomplete evidence", "missing evidence", "cannot satisfy",
                    "goal not met", "schema changed", "not available"}) ||
      failure_assessment.safety_risk >= 0.75F;

  if (request.active_plan.has_value() &&
      !request.active_plan->open_questions.empty()) {
    assessment.goal_gap = true;
  }

  return assessment;
}

std::vector<std::string> ReflectionEngine::detect_assumption_invalidations(
    const ReflectionAnalysisRequest& request) const {
  std::vector<std::string> invalidations;
  if (!request.belief_state.assumptions.has_value()) {
    return invalidations;
  }

  const auto evidence = collect_failure_text(request);
  const auto has_negation = contains_any(
      evidence,
      {" not ", "cannot", "can't", "unavailable", "denied", "missing",
       "blocked", "mismatch", "changed", "conflict", "failed"});
  if (!has_negation) {
    return invalidations;
  }

  for (const auto& assumption : *request.belief_state.assumptions) {
    if (assumption.empty()) {
      continue;
    }

    const auto keywords = extract_keywords(assumption);
    if (keywords.empty()) {
      continue;
    }

    std::size_t matches = 0U;
    for (const auto& keyword : keywords) {
      if (evidence.find(keyword) != std::string::npos) {
        ++matches;
      }
    }

    const auto minimum_matches = keywords.size() == 1U ? 1U : 2U;
    if (matches >= minimum_matches) {
      invalidations.push_back(assumption);
    }
  }

  return invalidations;
}

contracts::ReflectionDecision ReflectionEngine::project_reflection_decision(
    const ReflectionAnalysisRequest& request,
    const FailureAssessment& failure_assessment,
    const GoalGapAssessment& goal_gap_assessment,
    const std::vector<std::string>& belief_invalidations) const {
  contracts::ReflectionDecision decision;
  decision.request_id = request.request_id;
  if (request.goal_contract.goal_id.has_value() &&
      !request.goal_contract.goal_id->empty()) {
    decision.goal_id = request.goal_contract.goal_id;
  }
  if (request.latest_observation.observation_id.has_value() &&
      !request.latest_observation.observation_id->empty()) {
    decision.relevant_observation_refs =
        std::vector<std::string>{*request.latest_observation.observation_id};
  }
  if (request.latest_observation.created_at.has_value()) {
    decision.created_at = request.latest_observation.created_at;
  }

  const auto active_node_id = first_active_node_id(request);
  const auto effective_risk_limit =
      0.45F + clamp01(request.execution_hints.risk_tolerance) * 0.30F;
  const auto risk_too_high =
      failure_assessment.safety_risk > effective_risk_limit;

  ReflectionDecisionKind decision_kind = ReflectionDecisionKind::AbortSafe;
  std::string rationale;
  std::string hint_ref = "hint:reflection:abort_safe";
  float confidence = 0.25F;

  if (request.latest_observation.success.value_or(false) &&
      belief_invalidations.empty()) {
    decision_kind = ReflectionDecisionKind::Continue;
    confidence = 0.92F;
    hint_ref = "hint:reflection:continue";
    rationale = "latest observation indicates the active plan remains aligned";
  } else if (!belief_invalidations.empty()) {
    decision_kind = ReflectionDecisionKind::Replan;
    confidence = clamp01(std::max(
        config_.thresholds.replan_hint + 0.15F,
        failure_assessment.recoverability_score));
    hint_ref = "hint:reflection:replan";
    rationale = std::string("reflection detected invalidated assumptions: ") +
                join_strings(belief_invalidations, "; ");
  } else if (!risk_too_high && failure_assessment.retryable &&
             !goal_gap_assessment.goal_gap) {
    decision_kind = ReflectionDecisionKind::RetryStep;
    confidence = clamp01(std::max(0.72F, failure_assessment.recoverability_score));
    hint_ref = "hint:reflection:retry_step";
    rationale = "latest failure appears local and retryable without changing the plan";
  } else if (!risk_too_high && failure_assessment.safe_to_replan &&
             (goal_gap_assessment.goal_gap ||
              failure_assessment.recoverability_score >=
                  config_.thresholds.replan_hint)) {
    decision_kind = ReflectionDecisionKind::Replan;
    confidence = clamp01(std::max(0.65F, failure_assessment.recoverability_score));
    hint_ref = "hint:reflection:replan";
    rationale =
        "latest evidence indicates the active plan no longer matches the environment";
  } else {
    decision_kind = ReflectionDecisionKind::AbortSafe;
    confidence = clamp01(std::max(0.20F, 1.0F - failure_assessment.safety_risk));
    rationale = risk_too_high
                    ? "risk signals are too high to recommend retry or replan safely"
                    : "evidence is insufficient to recommend a safe retry or replan";
  }

  if (goal_gap_assessment.local_step_failure &&
      decision_kind == ReflectionDecisionKind::RetryStep) {
    rationale.append("; keeping recovery scoped to the current step");
  }
  if (active_node_id.has_value() &&
      decision_kind != ReflectionDecisionKind::Continue) {
    rationale.append("; active_node=");
    rationale.append(*active_node_id);
  }
  rationale.append("; failure_source=");
  rationale.append(failure_assessment.failure_source);

  decision.decision_kind = decision_kind;
  decision.rationale = std::move(rationale);
  decision.confidence = confidence;
  decision.hint_ref = std::move(hint_ref);
  decision.tags =
      std::vector<std::string>{"cognition", "reflection", decision_tag(decision_kind)};
  return decision;
}

contracts::ReflectionDecision ReflectionEngine::validate_reflection_contract(
    const ReflectionAnalysisRequest& request,
    contracts::ReflectionDecision decision) const {
  const auto guard = contracts::validate_reflection_decision_field_rules(decision);
  if (guard.ok) {
    return decision;
  }

  contracts::ReflectionDecision fallback;
  fallback.request_id = request.request_id;
  if (request.goal_contract.goal_id.has_value() &&
      !request.goal_contract.goal_id->empty()) {
    fallback.goal_id = request.goal_contract.goal_id;
  }
  fallback.decision_kind = ReflectionDecisionKind::AbortSafe;
  fallback.rationale =
      std::string("cognition.reflection_failed: ") + std::string(guard.reason);
  fallback.confidence = 0.20F;
  if (request.latest_observation.observation_id.has_value() &&
      !request.latest_observation.observation_id->empty()) {
    fallback.relevant_observation_refs =
        std::vector<std::string>{*request.latest_observation.observation_id};
  }
  if (request.latest_observation.created_at.has_value()) {
    fallback.created_at = request.latest_observation.created_at;
  }
  fallback.hint_ref = std::string("hint:reflection:abort_safe");
  fallback.tags =
      std::vector<std::string>{"cognition", "reflection", "abort_safe"};
  return fallback;
}

}  // namespace dasall::cognition::reflection