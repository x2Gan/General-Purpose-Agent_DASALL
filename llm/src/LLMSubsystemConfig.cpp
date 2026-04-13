#include "LLMSubsystemConfig.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "RuntimePolicySnapshot.h"

namespace {

[[nodiscard]] bool has_unique_values(const std::vector<std::string>& values) {
  std::vector<std::string> sorted_values = values;
  std::sort(sorted_values.begin(), sorted_values.end());
  return std::adjacent_find(sorted_values.begin(), sorted_values.end()) == sorted_values.end();
}

[[nodiscard]] bool is_reference_value(std::string_view value) {
  return value.starts_with("secret://") || value.starts_with("profile://") ||
         value.starts_with("header://");
}

[[nodiscard]] bool is_auth_reference_value(std::string_view value) {
  return value.starts_with("secret://") || value.starts_with("profile://");
}

void append_unique_values(std::vector<std::string>& destination,
                          const std::vector<std::string>& source) {
  for (const auto& value : source) {
    if (std::find(destination.begin(), destination.end(), value) == destination.end()) {
      destination.push_back(value);
    }
  }
}

[[nodiscard]] std::uint32_t clamp_timeout_ms(std::int64_t timeout_ms) {
  if (timeout_ms <= 0) {
    return 0U;
  }

  const auto max_timeout = static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max());
  return static_cast<std::uint32_t>(std::min(timeout_ms, max_timeout));
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

std::optional<LLMAdapterConfig> project_provider_to_adapter_config(
    const LLMSubsystemConfig& config,
    const ProviderDescriptor& descriptor,
    const ProviderRuntimeProjectionView& runtime_view) {
  if (!config.has_consistent_values() || !runtime_view.has_consistent_values() ||
      descriptor.provider_id.empty() || descriptor.adapter_family.empty() ||
      descriptor.base_url.empty() || descriptor.auth_ref.empty()) {
    return std::nullopt;
  }

  if (!is_auth_reference_value(descriptor.auth_ref) ||
      !has_unique_values(descriptor.header_refs) ||
      !std::all_of(descriptor.header_refs.begin(), descriptor.header_refs.end(),
                   [](const std::string& header_ref) { return is_reference_value(header_ref); })) {
    return std::nullopt;
  }

  const std::uint32_t timeout_ms = clamp_timeout_ms(config.timeout_policy.timeout_ms);
  if (timeout_ms == 0U) {
    return std::nullopt;
  }

  std::vector<std::string> capability_tags = descriptor.capability_tags;
  append_unique_values(capability_tags, runtime_view.runtime_tags);

  return LLMAdapterConfig{
      .adapter_id = runtime_view.provider_instance_id,
      .adapter_family = descriptor.adapter_family,
      .provider_instance_id = runtime_view.provider_instance_id,
      .base_url = descriptor.base_url,
      .base_url_alias = runtime_view.base_url_alias,
      .auth_ref = descriptor.auth_ref,
      .header_refs = descriptor.header_refs,
      .activation_flag = runtime_view.activation_flag,
      .snapshot_version = runtime_view.snapshot_version,
      .timeout_ms = timeout_ms,
      .max_retries = config.timeout_policy.retry_budget,
      .capability_tags = std::move(capability_tags),
  };
}

}  // namespace dasall::llm