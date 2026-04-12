#include "LLMSubsystemConfig.h"

#include <algorithm>
#include <utility>

#include "RuntimePolicySnapshot.h"

namespace {

[[nodiscard]] bool has_unique_values(const std::vector<std::string>& values) {
  std::vector<std::string> sorted_values = values;
  std::sort(sorted_values.begin(), sorted_values.end());
  return std::adjacent_find(sorted_values.begin(), sorted_values.end()) == sorted_values.end();
}

[[nodiscard]] std::map<std::string, dasall::llm::LLMStageRouteConfig> project_stage_routes(
    const dasall::profiles::ModelProfile& model_profile) {
  std::map<std::string, dasall::llm::LLMStageRouteConfig> stage_routes;
  for (const auto& [stage, route_policy] : model_profile.stage_routes) {
    stage_routes.emplace(stage, dasall::llm::LLMStageRouteConfig{
                                  .route = route_policy.route,
                                  .fallback_route = route_policy.fallback_route,
                                  .streaming_enabled = route_policy.streaming_enabled,
                              });
  }

  return stage_routes;
}

}  // namespace

namespace dasall::llm {

bool LLMSubsystemConfig::has_consistent_values() const {
  if (profile_id.empty() || stage_routes.empty() || allowed_prompt_releases.empty() ||
      trusted_sources.empty() || !prompt_asset_sources.has_consistent_values() ||
      !prompt_selector_overlay.has_consistent_values() ||
      !provider_catalog_sources.has_consistent_values() || !degrade_policy.has_consistent_values() ||
      !timeout_policy.has_consistent_values() || worker_threads == 0U) {
    return false;
  }

  return std::all_of(stage_routes.begin(), stage_routes.end(), [](const auto& entry) {
           return !entry.first.empty() && entry.second.has_consistent_values();
         }) &&
         has_unique_values(allowed_prompt_releases) && has_unique_values(trusted_sources) &&
         has_unique_values(tool_visibility_rules);
}

std::optional<LLMStageRouteConfig> LLMSubsystemConfig::stage_route_for(
    std::string_view stage) const {
  const auto stage_entry = stage_routes.find(std::string(stage));
  if (stage_entry == stage_routes.end()) {
    return std::nullopt;
  }

  return stage_entry->second;
}

prompt::PromptPolicyInput LLMSubsystemConfig::make_prompt_policy_input(
    std::uint32_t render_budget_tokens) const {
  return prompt::PromptPolicyInput{
      .profile_id = profile_id,
      .allowed_prompt_releases = allowed_prompt_releases,
      .trusted_sources = trusted_sources,
      .tool_visibility_rules = tool_visibility_rules,
      .render_budget_tokens = render_budget_tokens,
      .active_scene = prompt_selector_overlay.active_scene,
      .active_persona = prompt_selector_overlay.active_persona,
      .selected_release_scope = {},
      .selected_trusted_source = {},
      .visible_tools = {},
  };
}

std::optional<LLMSubsystemConfig> project_llm_subsystem_config(
    const profiles::RuntimePolicySnapshot& snapshot,
    const LLMSubsystemConfigOverlay& overlay) {
  if (!snapshot.has_consistent_values() || !overlay.has_consistent_values()) {
    return std::nullopt;
  }

  LLMSubsystemConfig config{
      .profile_id = snapshot.effective_profile_id(),
      .stage_routes = project_stage_routes(snapshot.model_profile()),
      .allowed_prompt_releases = snapshot.prompt_policy().allowed_prompt_releases,
      .trusted_sources = snapshot.prompt_policy().trusted_sources,
      .tool_visibility_rules = snapshot.prompt_policy().tool_visibility_rules,
      .prompt_asset_sources = overlay.prompt_asset_sources,
      .prompt_selector_overlay = overlay.prompt_selector_overlay,
      .provider_catalog_sources = overlay.provider_catalog_sources,
      .degrade_policy = LLMDegradeConfig{
          .fallback_chain = snapshot.degrade_policy().fallback_chain,
          .allow_model_failover = snapshot.degrade_policy().allow_model_failover,
          .allow_budget_degrade = snapshot.degrade_policy().allow_budget_degrade,
      },
      .timeout_policy = LLMTimeoutConfig{
          .timeout_ms = snapshot.timeout_policy().llm.timeout_ms,
          .retry_budget = snapshot.timeout_policy().llm.retry_budget,
          .circuit_breaker_threshold = snapshot.timeout_policy().llm.circuit_breaker_threshold,
      },
      .worker_threads = snapshot.worker_threads(),
  };

  if (!config.has_consistent_values()) {
    return std::nullopt;
  }

  return config;
}

}  // namespace dasall::llm