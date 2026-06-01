#include "planning/PlanCandidateRanker.h"

#include <algorithm>
#include <utility>

#include "IPlanner.h"

namespace dasall::cognition::planning {
namespace {

[[nodiscard]] float clamp_unit(float value) {
  return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] float derive_budget_pressure(
    const std::optional<BudgetContext>& budget_context) {
  if (!budget_context.has_value()) {
    return 0.0F;
  }

  auto pressure = clamp_unit(budget_context->budget_utilization);
  if (budget_context->near_budget_limit) {
    pressure = std::max(pressure, 0.85F);
  }

  return pressure;
}

[[nodiscard]] std::size_t candidate_complexity(const PlanCandidate& candidate) {
  const auto explicit_complexity = static_cast<std::size_t>(
      std::max(candidate.plan_graph.estimated_complexity, 1U));
  const auto structural_complexity = std::max<std::size_t>(
      1U, candidate.plan_graph.nodes.size() + candidate.plan_graph.open_questions.size());
  return std::max(explicit_complexity, structural_complexity);
}

[[nodiscard]] float derive_candidate_confidence(const PlanningRequest& request,
                                                const PlanCandidate& candidate) {
  auto confidence = clamp_unit(request.perception_result.confidence) * 0.55F +
                    clamp_unit(request.belief_state.confidence.value_or(0.0F)) * 0.45F;

  switch (candidate.candidate_kind) {
    case PlanCandidateKind::Canonical:
      confidence += 0.08F;
      break;
    case PlanCandidateKind::LeanExecution:
      confidence += 0.02F;
      break;
    case PlanCandidateKind::DirectResponseFallback:
      confidence += request.perception_result.task_type == "direct_response" ? 0.08F
                                                                              : -0.12F;
      break;
    case PlanCandidateKind::ClarificationFallback:
      confidence += request.perception_result.requires_clarification ? 0.12F : -0.08F;
      break;
  }

  if (!candidate.plan_graph.open_questions.empty()) {
    confidence -= 0.05F;
  }

  const auto complexity = candidate_complexity(candidate);
  if (complexity > 3U) {
    confidence -= 0.03F * static_cast<float>(complexity - 3U);
  }

  return clamp_unit(confidence);
}

[[nodiscard]] float derive_budget_fit(const PlanningRequest& request,
                                      const PlanCandidate& candidate) {
  const auto pressure = derive_budget_pressure(request.budget_context);
  const auto allowed_complexity = pressure >= 0.8F ? 2U : pressure >= 0.5F ? 3U : 4U;
  const auto complexity = candidate_complexity(candidate);

  auto budget_fit = 1.0F;
  if (complexity > allowed_complexity) {
    budget_fit -= 0.20F * static_cast<float>(complexity - allowed_complexity);
  } else if (complexity < allowed_complexity) {
    budget_fit -= 0.02F * static_cast<float>(allowed_complexity - complexity);
  }

  switch (candidate.candidate_kind) {
    case PlanCandidateKind::Canonical:
      budget_fit += pressure < 0.5F ? 0.04F : 0.00F;
      break;
    case PlanCandidateKind::LeanExecution:
      budget_fit += pressure >= 0.5F ? 0.10F : 0.04F;
      break;
    case PlanCandidateKind::DirectResponseFallback:
      budget_fit += pressure >= 0.8F ? 0.08F : -0.02F;
      break;
    case PlanCandidateKind::ClarificationFallback:
      budget_fit += request.perception_result.requires_clarification ? 0.10F : -0.05F;
      break;
  }

  if (!candidate.plan_graph.open_questions.empty() && pressure >= 0.5F) {
    budget_fit -= 0.06F;
  }

  return clamp_unit(budget_fit);
}

[[nodiscard]] float derive_ranking_score(float budget_pressure,
                                         float confidence,
                                         float budget_fit) {
  const auto budget_weight = std::clamp(0.30F + (budget_pressure * 0.35F), 0.30F, 0.65F);
  const auto confidence_weight = 1.0F - budget_weight;
  return clamp_unit((confidence * confidence_weight) + (budget_fit * budget_weight));
}

[[nodiscard]] int kind_priority(PlanCandidateKind kind) {
  switch (kind) {
    case PlanCandidateKind::Canonical:
      return 0;
    case PlanCandidateKind::LeanExecution:
      return 1;
    case PlanCandidateKind::DirectResponseFallback:
      return 2;
    case PlanCandidateKind::ClarificationFallback:
      return 3;
  }

  return 4;
}

}  // namespace

RankedPlanCandidates PlanCandidateRanker::rank_candidates(
    const PlanningRequest& request,
    std::vector<PlanCandidate> candidates,
    std::size_t max_output_candidates) const {
  RankedPlanCandidates ranked;
  if (candidates.empty()) {
    return ranked;
  }

  const auto budget_pressure = derive_budget_pressure(request.budget_context);
  for (auto& candidate : candidates) {
    candidate.confidence = derive_candidate_confidence(request, candidate);
    candidate.budget_fit = derive_budget_fit(request, candidate);
    candidate.ranking_score = derive_ranking_score(
        budget_pressure, candidate.confidence, candidate.budget_fit);
  }

  std::stable_sort(candidates.begin(), candidates.end(), [](const PlanCandidate& left,
                                                            const PlanCandidate& right) {
    if (left.ranking_score != right.ranking_score) {
      return left.ranking_score > right.ranking_score;
    }
    if (left.confidence != right.confidence) {
      return left.confidence > right.confidence;
    }
    if (left.budget_fit != right.budget_fit) {
      return left.budget_fit > right.budget_fit;
    }
    if (left.plan_graph.estimated_complexity != right.plan_graph.estimated_complexity) {
      return left.plan_graph.estimated_complexity < right.plan_graph.estimated_complexity;
    }
    return kind_priority(left.candidate_kind) < kind_priority(right.candidate_kind);
  });

  const auto output_count = std::max<std::size_t>(
      1U, std::min<std::size_t>(max_output_candidates, candidates.size()));
  candidates.resize(output_count);

  ranked.ranked_candidates = candidates;
  ranked.primary_candidate = ranked.ranked_candidates.front();
  if (ranked.ranked_candidates.size() > 1U) {
    ranked.backup_candidates.assign(ranked.ranked_candidates.begin() + 1U,
                                    ranked.ranked_candidates.end());
  }

  return ranked;
}

}  // namespace dasall::cognition::planning