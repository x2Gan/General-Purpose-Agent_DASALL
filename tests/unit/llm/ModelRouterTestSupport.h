#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "LLMSubsystemConfig.h"

#include "../../../llm/src/provider/ProviderCatalogRepository.h"
#include "../../../llm/src/route/ModelRouter.h"

namespace dasall::llm::test_support {

inline std::string aggregate_verification_state(
    const std::unordered_map<std::string, std::string>& verification_states) {
  bool saw_limited = false;
  for (const auto& [feature, state] : verification_states) {
    static_cast<void>(feature);
    const std::string normalized = [&]() {
      std::string value = state;
      std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
      });
      return value;
    }();

    if (normalized == "blocked") {
      return "blocked";
    }

    if (normalized != "verified") {
      saw_limited = true;
    }
  }

  return saw_limited ? "limited" : "verified";
}

inline provider::ProviderCatalogProvider make_provider(std::string provider_id,
                                                       std::vector<std::string> tags,
                                                       std::string trusted_source = "profiles",
                                                       bool activation_flag = true) {
  provider::ProviderCatalogProvider provider;
  provider.descriptor.provider_id = provider_id;
  provider.descriptor.adapter_family = "openai_compatible";
  provider.descriptor.api_family = "chat-completions";
  provider.descriptor.base_url = "https://example.invalid/" + provider_id;
  provider.descriptor.auth_ref = "secret://llm/providers/" + provider_id;
  provider.descriptor.capability_tags = tags;
  provider.descriptor.source_version = "2026.04.13";
  provider.runtime.display_name = provider_id;
  provider.runtime.auth_mode = "bearer_api_key";
  provider.runtime.trusted_source = std::move(trusted_source);
  provider.runtime.source_layer = "baseline";
  provider.runtime.base_url_alias = provider_id + "/default";
  provider.runtime.tags = std::move(tags);
  provider.runtime.mutable_overlay_fields = {"auth_ref", "header_refs", "base_url_alias", "activation_flag"};
  provider.runtime.activation_flag = activation_flag;
  return provider;
}

inline provider::ProviderModelMetadata make_model(
    std::string provider_id,
    std::string model_id,
    std::string tier_family,
    std::string latency_tier,
    std::string cost_tier,
    std::string reasoning_depth_tier,
    std::uint32_t context_window,
    std::uint32_t default_max_output_tokens,
    std::uint32_t max_output_tokens_hard_limit,
    bool supports_tools,
    bool supports_reasoning,
    bool supports_visible_reasoning,
    std::unordered_map<std::string, std::string> verification_states,
    std::vector<std::string> aliases = {}) {
  provider::ProviderModelMetadata model;
  model.summary.provider_id = std::move(provider_id);
  model.summary.model_id = model_id;
  model.summary.model_version = "2026.04.13";
  model.summary.tier_family = tier_family;
  model.summary.latency_tier = latency_tier;
  model.summary.cost_tier = cost_tier;
  model.summary.reasoning_depth_tier = reasoning_depth_tier;
  model.summary.context_window = context_window;
  model.summary.default_max_output_tokens = default_max_output_tokens;
  model.summary.max_output_tokens_hard_limit = max_output_tokens_hard_limit;
  model.summary.supports_tools = supports_tools;
  model.summary.supports_reasoning = supports_reasoning;
  model.summary.supports_visible_reasoning = supports_visible_reasoning;
  model.summary.supports_prompt_cache = true;
  model.summary.input_cache_hit_usd_per_1m = 0.01;
  model.summary.input_cache_miss_usd_per_1m = 0.10;
  model.summary.output_usd_per_1m = 1.00;
  model.summary.metadata_source_uri = "https://example.invalid/catalog";
  model.summary.metadata_effective_at = "2026-04-13";
  model.summary.verification_state = aggregate_verification_state(verification_states);
  model.display_name = model_id;
  model.reasoning_mode = supports_reasoning ? "thinking" : "non_thinking";
  model.source_layer = "baseline";
  model.pricing_ref = "pricing-2026.04.13";
  model.aliases = aliases.empty() ? std::vector<std::string>{model_id} : std::move(aliases);
  model.input_modalities = {"text"};
  model.feature_notes = {"test-fixture"};
  model.response_private_fields = supports_visible_reasoning
                                      ? std::vector<std::string>{"reasoning_content"}
                                      : std::vector<std::string>{};
  model.verification_states = std::move(verification_states);
  model.supports_streaming = true;
  model.supports_json_object = true;
  model.supports_json_schema = false;
  model.supports_native_stream_usage = false;
  return model;
}

inline provider::ProviderCatalogSnapshot make_default_catalog() {
  provider::ProviderCatalogSnapshot snapshot;
  snapshot.default_source_version = "2026.04.13";

  snapshot.providers = {
      make_provider("deepseek-prod", {"cloud", "external", "deepseek"}),
      make_provider("lan-ollama", {"lan", "internal"}),
      make_provider("local-runtime", {"local", "internal"}),
  };

  snapshot.models = {
      make_model("deepseek-prod",
                 "deepseek-chat",
                 "default",
                 "low",
                 "low",
                 "standard",
                 131072U,
                 4096U,
                 8192U,
                 true,
                 false,
                 false,
                 {{"tools", "verified"}, {"json_output", "verified"}}),
      make_model("deepseek-prod",
                 "deepseek-reasoner",
                 "reasoning",
                 "medium",
                 "medium",
                 "deep",
                 131072U,
                 32768U,
                 65536U,
                 true,
                 true,
                 true,
                 {{"tools", "needs_integration_validation"}, {"visible_reasoning", "verified"}}),
      make_model("lan-ollama",
                 "lan-general",
                 "default",
                 "medium",
                 "low",
                 "standard",
                 65536U,
                 4096U,
                 8192U,
                 true,
                 false,
                 false,
                 {{"tools", "verified"}}),
      make_model("local-runtime",
                 "local-small",
                 "economy",
                 "high",
                 "low",
                 "shallow",
                 8192U,
                 2048U,
                 4096U,
                 false,
                 false,
                 false,
                 {{"tools", "limited"}}),
  };

  return snapshot;
}

inline LLMSubsystemConfig make_config(std::string stage,
                                      std::string primary_route,
                                      std::optional<std::string> fallback_route = std::string("lan.general"),
                                      std::vector<std::string> degrade_chain = {"local.small"},
                                      bool allow_model_failover = true,
                                      bool allow_budget_degrade = true,
                                      std::vector<std::string> trusted_sources = {"profiles"}) {
  return LLMSubsystemConfig{
      .profile_id = "test-profile",
      .stage_routes = {{std::move(stage),
                        LLMStageRouteConfig{.route = std::move(primary_route),
                                            .fallback_route = std::move(fallback_route),
                                            .streaming_enabled = false}}},
      .allowed_prompt_releases = {"stable"},
      .trusted_sources = std::move(trusted_sources),
      .tool_visibility_rules = {"builtin:all"},
      .prompt_asset_sources = PromptAssetSourceConfig{},
      .prompt_selector_overlay = PromptSelectorOverlay{},
      .provider_catalog_sources = ProviderCatalogSourceConfig{},
      .degrade_policy = LLMDegradeConfig{.fallback_chain = std::move(degrade_chain),
                                         .allow_model_failover = allow_model_failover,
                                         .allow_budget_degrade = allow_budget_degrade},
      .timeout_policy = LLMTimeoutConfig{.timeout_ms = 4000, .retry_budget = 2U, .circuit_breaker_threshold = 6U},
      .worker_threads = 2U,
  };
}

inline route::ModelRouterHealthSnapshot make_health_snapshot(
    std::vector<route::ModelRouterHealthState> route_states = {}) {
  return route::ModelRouterHealthSnapshot{.route_states = std::move(route_states)};
}

inline std::string join_routes(const std::vector<std::string>& routes) {
  std::string joined;
  for (std::size_t index = 0U; index < routes.size(); ++index) {
    if (index > 0U) {
      joined.push_back(',');
    }

    joined.append(routes[index]);
  }

  return joined;
}

inline bool has_reason(const std::vector<std::string>& reasons, std::string_view reason) {
  return std::find_if(reasons.begin(), reasons.end(), [&](const std::string& value) {
           return value == reason;
         }) != reasons.end();
}

inline std::string join_audit_evidence(
    const std::vector<route::ModelRouterAuditEvidence>& audit_evidence) {
  std::string joined;
  for (std::size_t index = 0U; index < audit_evidence.size(); ++index) {
    if (index > 0U) {
      joined.push_back(';');
    }

    joined.append(audit_evidence[index].outcome);
    joined.push_back('@');
    joined.append(audit_evidence[index].route_preference);
    joined.push_back('@');
    joined.append(audit_evidence[index].route_id);
    joined.push_back('@');
    joined.append(join_routes(audit_evidence[index].reason_codes));
  }

  return joined;
}

inline bool has_audit_reason(const route::ModelRouterResolveResult& result,
                             std::string_view route_id,
                             std::string_view outcome,
                             std::string_view reason) {
  return std::find_if(result.audit_evidence.begin(), result.audit_evidence.end(),
                      [&](const route::ModelRouterAuditEvidence& evidence) {
                        return evidence.route_id == route_id && evidence.outcome == outcome &&
                               has_reason(evidence.reason_codes, reason);
                      }) != result.audit_evidence.end();
}

}  // namespace dasall::llm::test_support