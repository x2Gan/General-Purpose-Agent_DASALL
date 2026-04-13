#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "LLMAdapterConfig.h"
#include "provider/ProviderDescriptor.h"
#include "prompt/PromptPolicyInput.h"

namespace dasall::profiles {
class RuntimePolicySnapshot;
}

namespace dasall::llm {

struct LLMStageRouteConfig {
  std::string route;
  std::optional<std::string> fallback_route;
  bool streaming_enabled = false;

  [[nodiscard]] bool has_consistent_values() const {
    return !route.empty() && (!fallback_route.has_value() || !fallback_route->empty());
  }
};

struct PromptAssetSourceConfig {
  std::string baseline_root = "llm/assets/prompts";
  std::string deployment_root;
  std::string snapshot_cache_root;
  std::uint32_t cache_ttl_ms = 0U;
  bool signature_required = true;

  [[nodiscard]] bool has_consistent_values() const {
    return !baseline_root.empty() && (snapshot_cache_root.empty() || cache_ttl_ms > 0U);
  }
};

struct PromptSelectorOverlay {
  std::string active_scene;
  std::string active_persona;
  std::string default_prompt_bundle;

  [[nodiscard]] bool has_consistent_values() const {
    return true;
  }
};

struct ProviderCatalogSourceConfig {
  std::string baseline_root = "llm/assets/providers";
  std::string deployment_root;
  std::string snapshot_cache_root;
  bool signature_required = true;
  std::string merge_mode = "overlay";

  [[nodiscard]] bool has_consistent_values() const {
    return !baseline_root.empty() && !merge_mode.empty();
  }
};

struct ProviderRuntimeProjectionView {
  std::string provider_instance_id;
  std::string base_url_alias;
  std::string snapshot_version;
  std::vector<std::string> runtime_tags;
  bool activation_flag = true;

  [[nodiscard]] bool has_consistent_values() const {
    return !provider_instance_id.empty() && !base_url_alias.empty() && !snapshot_version.empty();
  }
};

struct LLMDegradeConfig {
  std::vector<std::string> fallback_chain;
  bool allow_model_failover = false;
  bool allow_budget_degrade = false;

  [[nodiscard]] bool has_consistent_values() const {
    return !fallback_chain.empty();
  }
};

struct LLMTimeoutConfig {
  std::int64_t timeout_ms = 0;
  std::uint32_t retry_budget = 0U;
  std::uint32_t circuit_breaker_threshold = 0U;

  [[nodiscard]] bool has_consistent_values() const {
    return timeout_ms > 0 && circuit_breaker_threshold > 0U;
  }
};

struct LLMSubsystemConfigOverlay {
  PromptAssetSourceConfig prompt_asset_sources;
  PromptSelectorOverlay prompt_selector_overlay;
  ProviderCatalogSourceConfig provider_catalog_sources;

  [[nodiscard]] bool has_consistent_values() const {
    return prompt_asset_sources.has_consistent_values() &&
           prompt_selector_overlay.has_consistent_values() &&
           provider_catalog_sources.has_consistent_values();
  }
};

struct LLMSubsystemConfig {
  std::string profile_id;
  std::map<std::string, LLMStageRouteConfig> stage_routes;
  std::vector<std::string> allowed_prompt_releases;
  std::vector<std::string> trusted_sources;
  std::vector<std::string> tool_visibility_rules;
  PromptAssetSourceConfig prompt_asset_sources;
  PromptSelectorOverlay prompt_selector_overlay;
  ProviderCatalogSourceConfig provider_catalog_sources;
  LLMDegradeConfig degrade_policy;
  LLMTimeoutConfig timeout_policy;
  std::uint32_t worker_threads = 1U;

  [[nodiscard]] bool has_consistent_values() const;
  [[nodiscard]] std::optional<LLMStageRouteConfig> stage_route_for(std::string_view stage) const;
  [[nodiscard]] prompt::PromptPolicyInput make_prompt_policy_input(
      std::uint32_t render_budget_tokens = 0U) const;
};

[[nodiscard]] std::optional<LLMSubsystemConfig> project_llm_subsystem_config(
    const profiles::RuntimePolicySnapshot& snapshot,
    const LLMSubsystemConfigOverlay& overlay = {});

[[nodiscard]] std::optional<LLMAdapterConfig> project_provider_to_adapter_config(
  const LLMSubsystemConfig& config,
  const ProviderDescriptor& descriptor,
  const ProviderRuntimeProjectionView& runtime_view);

}  // namespace dasall::llm