#include "ModelRouter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using ModelRouterHealthSnapshot = dasall::llm::route::ModelRouterHealthSnapshot;
using ModelRouterAuditEvidence = dasall::llm::route::ModelRouterAuditEvidence;
using ModelRouterResolveResult = dasall::llm::route::ModelRouterResolveResult;
using ProviderCatalogProvider = dasall::llm::provider::ProviderCatalogProvider;
using ProviderCatalogSnapshot = dasall::llm::provider::ProviderCatalogSnapshot;
using ProviderModelMetadata = dasall::llm::provider::ProviderModelMetadata;

struct RoutePreference {
  std::string raw_route;
  std::string locality;
  std::string preferred_tier;
  std::size_t order = 0U;
};

struct ScoredCandidate {
  std::string route_id;
  std::string route_preference;
  std::string provider_id;
  std::string model_id;
  std::string preferred_tier;
  std::string actual_tier;
  std::size_t route_order = 0U;
  int score = 0;
  std::vector<std::string> reason_codes;
};

std::string to_lower_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

bool contains_value(const std::vector<std::string>& values, std::string_view needle) {
  return std::find(values.begin(), values.end(), needle) != values.end();
}

std::string make_route_id(std::string_view provider_id, std::string_view model_id) {
  return std::string(provider_id) + "/" + std::string(model_id);
}

void append_unique_reason(std::vector<std::string>& reasons, std::string reason) {
  if (!contains_value(reasons, reason)) {
    reasons.push_back(std::move(reason));
  }
}

void append_unique_reasons(std::vector<std::string>& reasons,
                           const std::vector<std::string>& new_reasons) {
  for (const auto& reason : new_reasons) {
    append_unique_reason(reasons, reason);
  }
}

void append_audit_evidence(std::vector<ModelRouterAuditEvidence>& audit_evidence,
                           std::string route_id,
                           std::string route_preference,
                           std::string outcome,
                           std::vector<std::string> reason_codes) {
  if (route_id.empty()) {
    return;
  }

  audit_evidence.push_back(ModelRouterAuditEvidence{
      .route_id = std::move(route_id),
      .route_preference = std::move(route_preference),
      .outcome = std::move(outcome),
      .reason_codes = std::move(reason_codes),
  });
}

std::string canonical_tier(std::string_view raw_tier) {
  const std::string tier = to_lower_copy(std::string(raw_tier));
  if (tier == "general") {
    return "default";
  }

  if (tier == "small") {
    return "economy";
  }

  if (tier == "tool-first") {
    return "tool_first";
  }

  return tier;
}

int tier_rank(std::string_view tier) {
  const std::string canonical = canonical_tier(tier);
  if (canonical == "economy") {
    return 0;
  }

  if (canonical == "default" || canonical == "tool_first") {
    return 1;
  }

  if (canonical == "reasoning") {
    return 2;
  }

  if (canonical == "premium") {
    return 3;
  }

  return 1;
}

RoutePreference parse_route_preference(std::string raw_route, std::size_t order) {
  RoutePreference preference;
  preference.raw_route = std::move(raw_route);
  preference.order = order;

  const auto separator = preference.raw_route.find('.');
  if (separator == std::string::npos) {
    preference.locality = to_lower_copy(preference.raw_route);
    preference.preferred_tier = canonical_tier(preference.raw_route);
    return preference;
  }

  preference.locality = to_lower_copy(preference.raw_route.substr(0U, separator));
  preference.preferred_tier = canonical_tier(preference.raw_route.substr(separator + 1U));
  return preference;
}

bool state_is_blocked(std::string_view state) {
  return to_lower_copy(std::string(state)) == "blocked";
}

bool state_is_verified(std::string_view state) {
  return to_lower_copy(std::string(state)) == "verified";
}

bool state_requires_validation(std::string_view state) {
  const std::string normalized = to_lower_copy(std::string(state));
  return normalized == "needs_integration_validation" || normalized == "declared" ||
         normalized == "limited";
}

bool model_matches_locality(const ProviderCatalogProvider& provider, std::string_view locality) {
  if (locality.empty()) {
    return true;
  }

  if (locality == "builtin_only") {
    return false;
  }

  return contains_value(provider.runtime.tags, locality) ||
         contains_value(provider.descriptor.capability_tags, locality);
}

bool provider_source_is_trusted(const dasall::llm::LLMSubsystemConfig& config,
                                const ProviderCatalogProvider& provider) {
  return config.trusted_sources.empty() ||
         contains_value(config.trusted_sources, provider.runtime.trusted_source);
}

bool is_reasoning_stage(std::string_view stage, std::string_view task_type) {
  const std::string normalized_stage = to_lower_copy(std::string(stage));
  const std::string normalized_task_type = to_lower_copy(std::string(task_type));
  return normalized_stage == "planner" || normalized_stage == "replan" ||
         normalized_stage == "diagnosis" || normalized_task_type == "plan" ||
         normalized_task_type == "cross_doc_compare" ||
         normalized_task_type == "root_cause_analysis";
}

bool is_interactive_sla(std::string_view latency_sla_tier) {
  const std::string normalized = to_lower_copy(std::string(latency_sla_tier));
  return normalized == "interactive" || normalized == "low" || normalized == "fast";
}

bool is_hard_budget(std::string_view budget_tier) {
  const std::string normalized = to_lower_copy(std::string(budget_tier));
  return normalized == "hard_cap" || normalized == "economy" || normalized == "low";
}

bool is_summary_like_task(std::string_view task_type) {
  const std::string normalized = to_lower_copy(std::string(task_type));
  return normalized == "summary" || normalized == "summarize" || normalized == "extract" ||
         normalized == "rewrite" || normalized == "format_convert";
}

bool context_window_is_sufficient(const ProviderModelMetadata& model,
                                  const dasall::llm::ModelSelectionHint& hint) {
  const std::uint64_t estimated_input_tokens = hint.estimated_input_tokens;
  const std::uint64_t target_output_tokens = hint.target_output_tokens;
  const std::uint64_t context_window = model.summary.context_window;

  if (estimated_input_tokens > context_window) {
    return false;
  }

  if (target_output_tokens == 0U) {
    return true;
  }

  return estimated_input_tokens + target_output_tokens <= context_window;
}

bool output_budget_is_sufficient(const ProviderModelMetadata& model,
                                 const dasall::llm::ModelSelectionHint& hint) {
  return hint.target_output_tokens == 0U ||
         hint.target_output_tokens <= model.summary.max_output_tokens_hard_limit;
}

std::vector<RoutePreference> build_route_preferences(
    const dasall::llm::LLMSubsystemConfig& config,
    const dasall::llm::ModelSelectionHint& hint,
    const dasall::llm::LLMStageRouteConfig& stage_route) {
  std::vector<RoutePreference> preferences;
  std::vector<std::string> ordered_routes = {stage_route.route};

  if (stage_route.fallback_route.has_value()) {
    ordered_routes.push_back(*stage_route.fallback_route);
  }

  if (config.degrade_policy.allow_model_failover ||
      (config.degrade_policy.allow_budget_degrade && is_hard_budget(hint.budget_tier))) {
    ordered_routes.insert(ordered_routes.end(),
                          config.degrade_policy.fallback_chain.begin(),
                          config.degrade_policy.fallback_chain.end());
  }

  std::vector<std::string> deduplicated_routes;
  for (const auto& route : ordered_routes) {
    if (route.empty() || contains_value(deduplicated_routes, route)) {
      continue;
    }

    deduplicated_routes.push_back(route);
  }

  preferences.reserve(deduplicated_routes.size());
  for (std::size_t index = 0U; index < deduplicated_routes.size(); ++index) {
    preferences.push_back(parse_route_preference(deduplicated_routes[index], index));
  }

  return preferences;
}

void append_stage_and_route_reasons(std::vector<std::string>& reasons,
                                    std::string_view stage,
                                    const RoutePreference& route_preference,
                                    std::string_view actual_tier) {
  append_unique_reason(reasons, std::string("stage:") + std::string(stage));
  append_unique_reason(reasons, std::string("route_preference:") + route_preference.raw_route);

  const int preferred_rank = tier_rank(route_preference.preferred_tier);
  const int actual_rank = tier_rank(actual_tier);
  if (actual_rank > preferred_rank) {
    append_unique_reason(reasons, "tier_upgraded");
  } else if (actual_rank < preferred_rank) {
    append_unique_reason(reasons, "tier_degraded");
  }
}

std::optional<ScoredCandidate> score_candidate(
    const dasall::llm::LLMSubsystemConfig& config,
    const dasall::llm::ModelSelectionHint& hint,
    const RoutePreference& route_preference,
    const ProviderCatalogProvider& provider,
    const ProviderModelMetadata& model,
    const ModelRouterHealthSnapshot& health_snapshot,
    std::vector<std::string>& rejection_reasons) {
  const std::string route_id = make_route_id(provider.descriptor.provider_id, model.summary.model_id);

  if (!provider.runtime.activation_flag) {
    append_unique_reason(rejection_reasons, "provider_inactive");
    return std::nullopt;
  }

  if (!provider_source_is_trusted(config, provider)) {
    append_unique_reason(rejection_reasons, "provider_source_untrusted");
    return std::nullopt;
  }

  if (state_is_blocked(model.summary.verification_state)) {
    append_unique_reason(rejection_reasons, "verification_blocked");
    return std::nullopt;
  }

  if (health_snapshot.route_is_blocked(provider.descriptor.provider_id, model.summary.model_id)) {
    append_unique_reason(rejection_reasons, "health_blocked");
    return std::nullopt;
  }

  if (!context_window_is_sufficient(model, hint)) {
    append_unique_reason(rejection_reasons, "context_window_insufficient");
    return std::nullopt;
  }

  if (!output_budget_is_sufficient(model, hint)) {
    append_unique_reason(rejection_reasons, "output_limit_exceeded");
    return std::nullopt;
  }

  if (hint.requires_tools) {
    if (!model.summary.supports_tools) {
      append_unique_reason(rejection_reasons, "tools_unsupported");
      return std::nullopt;
    }

    const std::string tool_state = model.verification_state_for("tools");
    if (!state_is_verified(tool_state)) {
      append_unique_reason(rejection_reasons,
                           state_requires_validation(tool_state) ? "tools_unverified"
                                                                 : "tools_unsupported");
      return std::nullopt;
    }
  }

  if (hint.requires_reasoning && !model.summary.supports_reasoning) {
    append_unique_reason(rejection_reasons, "reasoning_unsupported");
    return std::nullopt;
  }

  ScoredCandidate candidate;
  candidate.route_id = route_id;
  candidate.route_preference = route_preference.raw_route;
  candidate.provider_id = provider.descriptor.provider_id;
  candidate.model_id = model.summary.model_id;
  candidate.preferred_tier = route_preference.preferred_tier;
  candidate.actual_tier = canonical_tier(model.summary.tier_family);
  candidate.route_order = route_preference.order;

  candidate.score += static_cast<int>(1000 - (route_preference.order * 200));
  append_unique_reason(candidate.reason_codes,
                       route_preference.order == 0U ? "selected_primary_route"
                                                    : "selected_from_fallback_chain");

  if (candidate.actual_tier == candidate.preferred_tier ||
      (candidate.preferred_tier == "tool_first" && model.summary.supports_tools)) {
    candidate.score += 250;
    append_unique_reason(candidate.reason_codes, "preferred_tier_match");
  } else if (std::abs(tier_rank(candidate.actual_tier) - tier_rank(candidate.preferred_tier)) ==
             1) {
    candidate.score += 120;
    append_unique_reason(candidate.reason_codes, "adjacent_tier_match");
  }

  if (hint.requires_reasoning && model.summary.supports_reasoning) {
    candidate.score += 400;
    append_unique_reason(candidate.reason_codes, "requires_reasoning");
  } else if (is_reasoning_stage(hint.stage, hint.task_type) && model.summary.supports_reasoning) {
    candidate.score += 220;
    append_unique_reason(candidate.reason_codes, "reasoning_task_bias");
  }

  if (is_interactive_sla(hint.latency_sla_tier)) {
    if (to_lower_copy(model.summary.latency_tier) == "low") {
      candidate.score += 200;
      append_unique_reason(candidate.reason_codes, "interactive_latency_bias");
    } else if (to_lower_copy(model.summary.latency_tier) == "medium") {
      candidate.score += 80;
    }
  }

  if (is_hard_budget(hint.budget_tier)) {
    if (to_lower_copy(model.summary.cost_tier) == "low") {
      candidate.score += 180;
      append_unique_reason(candidate.reason_codes, "budget_low_cost");
    } else if (to_lower_copy(model.summary.cost_tier) == "medium") {
      candidate.score += 80;
    }
  }

  if (is_interactive_sla(hint.latency_sla_tier) && is_hard_budget(hint.budget_tier) &&
      !hint.requires_reasoning) {
    if (candidate.actual_tier == "default" || candidate.actual_tier == "economy") {
      candidate.score += 220;
      append_unique_reason(candidate.reason_codes, "interactive_hard_cap_downgrade");
    } else if (candidate.actual_tier == "reasoning" || candidate.actual_tier == "premium") {
      candidate.score -= 220;
      append_unique_reason(candidate.reason_codes, "interactive_hard_cap_penalty");
    }
  }

  if (hint.requires_tools) {
    candidate.score += 160;
    append_unique_reason(candidate.reason_codes, "requires_tools");
  }

  if (hint.prefers_visible_reasoning && model.summary.supports_visible_reasoning &&
      state_is_verified(model.verification_state_for("visible_reasoning"))) {
    candidate.score += 90;
    append_unique_reason(candidate.reason_codes, "visible_reasoning_preferred");
  }

  if (hint.target_output_tokens > model.summary.default_max_output_tokens) {
    candidate.score += 100;
    append_unique_reason(candidate.reason_codes, "large_output_supported");
  }

  if (hint.estimated_input_tokens > (model.summary.context_window * 3U) / 4U &&
      is_summary_like_task(hint.task_type) && !model.summary.supports_reasoning) {
    candidate.score += 100;
    append_unique_reason(candidate.reason_codes, "long_context_summary_bias");
  }

  if (state_is_verified(model.summary.verification_state)) {
    candidate.score += 40;
    append_unique_reason(candidate.reason_codes, "verification_verified");
  }

  const std::uint32_t health_failures =
      health_snapshot.consecutive_failures_for(provider.descriptor.provider_id, model.summary.model_id);
  if (health_failures > 0U) {
    candidate.score -= static_cast<int>(health_failures * 60U);
    append_unique_reason(candidate.reason_codes, "health_failure_penalty");
  }

  if (hint.previous_route_failures > 0U) {
    if (route_preference.order == 0U) {
      candidate.score -= static_cast<int>(hint.previous_route_failures * 150U);
      append_unique_reason(candidate.reason_codes, "previous_route_failure_penalty");
    } else {
      candidate.score += static_cast<int>(std::min<std::uint32_t>(hint.previous_route_failures, 2U) *
                                          45U);
      append_unique_reason(candidate.reason_codes, "fallback_recovery_bias");
    }
  }

  append_stage_and_route_reasons(candidate.reason_codes, hint.stage, route_preference, candidate.actual_tier);
  return candidate;
}

bool candidate_is_better(const ScoredCandidate& current, const ScoredCandidate& candidate) {
  if (candidate.score != current.score) {
    return candidate.score > current.score;
  }

  if (candidate.route_order != current.route_order) {
    return candidate.route_order < current.route_order;
  }

  return candidate.route_id < current.route_id;
}

}  // namespace

namespace dasall::llm::route {

std::string ModelRouterHealthState::route_key() const {
  return provider_id + "/" + model_id;
}

const ModelRouterHealthState* ModelRouterHealthSnapshot::find_route_state(
    std::string_view provider_id,
    std::string_view model_id) const {
  const auto it = std::find_if(route_states.begin(), route_states.end(),
                               [&](const ModelRouterHealthState& state) {
                                 return state.provider_id == provider_id && state.model_id == model_id;
                               });
  if (it == route_states.end()) {
    return nullptr;
  }

  return &(*it);
}

bool ModelRouterHealthSnapshot::route_is_blocked(std::string_view provider_id,
                                                 std::string_view model_id) const {
  const auto* state = find_route_state(provider_id, model_id);
  return state != nullptr && state->blocked;
}

std::uint32_t ModelRouterHealthSnapshot::consecutive_failures_for(std::string_view provider_id,
                                                                  std::string_view model_id) const {
  const auto* state = find_route_state(provider_id, model_id);
  return state == nullptr ? 0U : state->consecutive_failures;
}

bool ModelRouter::init(const LLMSubsystemConfig& config) {
  if (!config.has_consistent_values()) {
    return false;
  }

  config_ = config;
  initialized_ = true;
  return true;
}

ModelRouterResolveResult ModelRouter::resolve(
    const ModelSelectionHint& selection_hint,
    const provider::ProviderCatalogSnapshot& catalog_snapshot,
    const ModelRouterHealthSnapshot& health_snapshot) const {
  ModelRouterResolveResult result;
  if (!initialized_) {
    result.selection_reason_codes = {"router_not_initialized"};
    return result;
  }

  if (!catalog_snapshot.has_consistent_values()) {
    result.selection_reason_codes = {"invalid_provider_catalog"};
    return result;
  }

  const auto stage_route = config_.stage_route_for(selection_hint.stage);
  if (!stage_route.has_value()) {
    result.selection_reason_codes = {"stage_route_missing"};
    return result;
  }

  const std::vector<RoutePreference> route_preferences =
      build_route_preferences(config_, selection_hint, *stage_route);

  std::unordered_map<std::string, ScoredCandidate> best_candidates;
  std::vector<std::string> rejection_reasons;
  std::vector<ModelRouterAuditEvidence> rejected_audit_evidence;

  for (const auto& route_preference : route_preferences) {
    for (const auto& model : catalog_snapshot.models) {
      const std::string route_id = make_route_id(model.summary.provider_id, model.summary.model_id);
      const auto* provider = catalog_snapshot.find_provider(model.summary.provider_id);
      if (provider == nullptr) {
        append_unique_reason(rejection_reasons, "provider_missing");
        append_audit_evidence(rejected_audit_evidence,
                              route_id,
                              route_preference.raw_route,
                              "rejected_hard_filter",
                              {"provider_missing"});
        continue;
      }

      if (!model_matches_locality(*provider, route_preference.locality)) {
        continue;
      }

      std::vector<std::string> candidate_rejection_reasons;
      auto candidate = score_candidate(config_,
                                       selection_hint,
                                       route_preference,
                                       *provider,
                                       model,
                                       health_snapshot,
                                       candidate_rejection_reasons);
      if (!candidate.has_value()) {
        append_unique_reasons(rejection_reasons, candidate_rejection_reasons);
        append_audit_evidence(rejected_audit_evidence,
                              route_id,
                              route_preference.raw_route,
                              "rejected_hard_filter",
                              std::move(candidate_rejection_reasons));
        continue;
      }

      const auto existing = best_candidates.find(candidate->route_id);
      if (existing == best_candidates.end() || candidate_is_better(existing->second, *candidate)) {
        best_candidates[candidate->route_id] = *candidate;
      }
    }
  }

  if (best_candidates.empty()) {
    if (rejection_reasons.empty()) {
      rejection_reasons.push_back("no_candidate_available");
    }

    append_unique_reason(rejection_reasons, "no_candidate_after_hard_filter");
    result.selection_reason_codes = std::move(rejection_reasons);
    result.audit_evidence = std::move(rejected_audit_evidence);
    return result;
  }

  std::vector<ScoredCandidate> ordered_candidates;
  ordered_candidates.reserve(best_candidates.size());
  for (const auto& [route_id, candidate] : best_candidates) {
    static_cast<void>(route_id);
    ordered_candidates.push_back(candidate);
  }

  std::sort(ordered_candidates.begin(), ordered_candidates.end(), [](const ScoredCandidate& left,
                                                                    const ScoredCandidate& right) {
    if (left.score != right.score) {
      return left.score > right.score;
    }

    if (left.route_order != right.route_order) {
      return left.route_order < right.route_order;
    }

    return left.route_id < right.route_id;
  });

  ResolvedModelRoute route;
  route.stage = selection_hint.stage;
  route.primary_route = ordered_candidates.front().route_id;
  route.streaming_enabled = stage_route->streaming_enabled;

  for (std::size_t index = 1U; index < ordered_candidates.size(); ++index) {
    route.fallback_routes.push_back(ordered_candidates[index].route_id);
  }

  result.resolved_route = std::move(route);
  result.selection_reason_codes = ordered_candidates.front().reason_codes;
  if (!ordered_candidates.front().reason_codes.empty() && !result.resolved_route->fallback_routes.empty()) {
    append_unique_reason(result.selection_reason_codes, "fallback_chain_prepared");
  }

  result.audit_evidence.reserve(ordered_candidates.size() + rejected_audit_evidence.size());
  for (std::size_t index = 0U; index < ordered_candidates.size(); ++index) {
    std::vector<std::string> audit_reason_codes = ordered_candidates[index].reason_codes;
    if (index == 0U && !result.resolved_route->fallback_routes.empty()) {
      append_unique_reason(audit_reason_codes, "fallback_chain_prepared");
    }

    append_audit_evidence(result.audit_evidence,
                          ordered_candidates[index].route_id,
                          ordered_candidates[index].route_preference,
                          index == 0U ? "selected_primary" : "selected_fallback",
                          std::move(audit_reason_codes));
  }

  result.audit_evidence.insert(result.audit_evidence.end(),
                               std::make_move_iterator(rejected_audit_evidence.begin()),
                               std::make_move_iterator(rejected_audit_evidence.end()));

  return result;
}

}  // namespace dasall::llm::route