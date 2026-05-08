#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include "LLMSubsystemConfig.h"
#include "RuntimePolicySnapshot.h"
#include "config/InstallLayout.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::profiles::RuntimePolicySnapshot make_runtime_policy_snapshot() {
  using dasall::profiles::CapabilityCachePolicy;
  using dasall::profiles::DegradePolicy;
  using dasall::profiles::ExecutionPolicy;
  using dasall::profiles::ModelProfile;
  using dasall::profiles::ModelRoutePolicy;
  using dasall::profiles::OpsPolicy;
  using dasall::profiles::PromptPolicy;
  using dasall::profiles::RuntimePolicySnapshot;
  using dasall::profiles::TimeoutBudget;
  using dasall::profiles::TimeoutPolicy;
  using dasall::profiles::TokenBudgetPolicy;

  return RuntimePolicySnapshot{
      12U,
      "desktop_full",
      dasall::contracts::RuntimeBudget{
          .max_tokens = 8192U,
          .max_turns = 16U,
          .max_tool_calls = 8U,
          .max_latency_ms = 5000U,
          .max_replan_count = 2U,
      },
      ModelProfile{
          .stage_routes = {
              {"planner", ModelRoutePolicy{.route = "cloud.reasoning",
                                            .fallback_route = "lan.general",
                                            .streaming_enabled = true}},
              {"responder", ModelRoutePolicy{.route = "cloud.general",
                                              .fallback_route = "local.small",
                                              .streaming_enabled = false}},
          },
      },
      TokenBudgetPolicy{.max_input_tokens = 4096U,
                        .max_output_tokens = 1024U,
                        .max_history_turns = 8U,
                        .compression_threshold = 3000U},
      PromptPolicy{.allowed_prompt_releases = {"stable", "fallback"},
                   .trusted_sources = {"profiles", "infra_config"},
                   .tool_visibility_rules = {"builtin:all", "mcp:read_only"}},
      CapabilityCachePolicy{.refresh_interval_ms = 1000,
                            .expire_after_ms = 5000,
                            .stale_read_allowed = false,
                            .failure_backoff_ms = 500},
      DegradePolicy{.fallback_chain = {"lan.general", "local.small"},
                    .allow_model_failover = true,
                    .allow_budget_degrade = true},
      TimeoutPolicy{.llm = TimeoutBudget{.timeout_ms = 4500,
                                         .retry_budget = 2U,
                                         .circuit_breaker_threshold = 4U},
                    .tool = TimeoutBudget{.timeout_ms = 1500,
                                          .retry_budget = 1U,
                                          .circuit_breaker_threshold = 3U},
                    .mcp = TimeoutBudget{.timeout_ms = 1500,
                                         .retry_budget = 1U,
                                         .circuit_breaker_threshold = 3U},
                    .workflow = TimeoutBudget{.timeout_ms = 3000,
                                              .retry_budget = 1U,
                                              .circuit_breaker_threshold = 3U}},
      ExecutionPolicy{.requires_high_risk_confirmation = true,
                      .safe_mode_enabled = true,
                      .audit_level = "full",
                      .allowed_tool_domains = {"builtin", "mcp"}},
      OpsPolicy{.log_level = "info",
                .metrics_granularity = "full",
                .trace_sample_ratio = 0.25,
                .remote_diagnostics_enabled = true,
                .upgrade_strategy = "rolling"},
      3U,
  };
}

void test_projector_maps_runtime_policy_snapshot_into_llm_consumer_view() {
  using dasall::llm::project_llm_subsystem_config;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto install_layout = dasall::infra::config::resolve_install_layout();
  const auto config = project_llm_subsystem_config(make_runtime_policy_snapshot());

  assert_true(config.has_value(),
              "LLMSubsystemConfig projector should accept a consistent runtime policy snapshot");
  assert_true(config->has_consistent_values(),
              "projected LLMSubsystemConfig should preserve llm-facing invariants");
  assert_equal(std::string("desktop_full"), config->profile_id,
               "projected config should preserve the effective profile identity");
  assert_equal(2, static_cast<int>(config->stage_routes.size()),
               "projected config should keep all model profile stage routes");
  assert_equal(std::string("cloud.reasoning"), config->stage_routes.at("planner").route,
               "planner route should come directly from the model profile stage map");
  assert_true(config->stage_routes.at("planner").streaming_enabled,
              "projected planner route should preserve streaming_enabled policy");
  assert_equal(install_layout.llm_prompts_root.string(),
               config->prompt_asset_sources.baseline_root,
               "default prompt baseline root should follow the install-aware layout prompt root");
  assert_equal(install_layout.llm_providers_root.string(),
               config->provider_catalog_sources.baseline_root,
               "default provider baseline root should follow the install-aware layout provider root");
  assert_true(config->prompt_selector_overlay.active_scene.empty(),
              "default active_scene should stay empty so PromptRegistry can fall back to profile/package selectors");
  assert_true(config->prompt_selector_overlay.active_persona.empty(),
              "default active_persona should stay empty so PromptRegistry can fall back to profile/package selectors");
  assert_equal(4500, static_cast<int>(config->timeout_policy.timeout_ms),
               "llm timeout policy should project timeout_ms from profiles timeout_policy.llm");
  assert_equal(2, static_cast<int>(config->timeout_policy.retry_budget),
               "llm timeout policy should project retry_budget from profiles timeout_policy.llm");
  assert_equal(3, static_cast<int>(config->worker_threads),
               "worker_threads should stay aligned with the upstream runtime snapshot");
}

void test_projector_applies_module_local_overlay_inputs() {
  using dasall::llm::LLMSubsystemConfigOverlay;
  using dasall::llm::project_llm_subsystem_config;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  LLMSubsystemConfigOverlay overlay;
  overlay.prompt_asset_sources.deployment_root = "deploy/llm/prompts";
  overlay.prompt_asset_sources.snapshot_cache_root = "runtime/cache/prompts";
  overlay.prompt_asset_sources.cache_ttl_ms = 300000U;
  overlay.prompt_selector_overlay.active_scene = "factory.operator";
  overlay.prompt_selector_overlay.active_persona = "operator";
  overlay.prompt_selector_overlay.default_prompt_bundle = "ops_bundle";
  overlay.provider_catalog_sources.deployment_root = "deploy/llm/providers";
  overlay.provider_catalog_sources.snapshot_cache_root = "runtime/cache/providers";
  overlay.provider_catalog_sources.merge_mode = "overlay";

  const auto config = project_llm_subsystem_config(make_runtime_policy_snapshot(), overlay);

  assert_true(config.has_value(),
              "projector should accept module-local overlay inputs in addition to profile snapshot fields");
  assert_equal(std::string("deploy/llm/prompts"), config->prompt_asset_sources.deployment_root,
               "prompt deployment root should remain a pure llm-local overlay input");
  assert_equal(std::string("runtime/cache/prompts"), config->prompt_asset_sources.snapshot_cache_root,
               "prompt snapshot cache root should remain a pure llm-local overlay input");
  assert_equal(std::string("factory.operator"), config->prompt_selector_overlay.active_scene,
               "active_scene should be carried through as a selector overlay rather than encoded into shared profiles");
  assert_equal(std::string("operator"), config->prompt_selector_overlay.active_persona,
               "active_persona should be carried through as a selector overlay rather than encoded into shared profiles");
  assert_equal(std::string("ops_bundle"), config->prompt_selector_overlay.default_prompt_bundle,
               "default_prompt_bundle should remain a module-local prompt selection hint");

  const auto prompt_policy_input = config->make_prompt_policy_input(2048U);
  assert_equal(std::string("factory.operator"), prompt_policy_input.active_scene,
               "make_prompt_policy_input should project active_scene into the PromptPolicyInput handoff");
  assert_equal(std::string("operator"), prompt_policy_input.active_persona,
               "make_prompt_policy_input should project active_persona into the PromptPolicyInput handoff");
  assert_equal(2048, static_cast<int>(prompt_policy_input.render_budget_tokens),
               "make_prompt_policy_input should accept render budget at the llm consumer boundary");

  const auto planner_route = config->stage_route_for("planner");
  assert_true(planner_route.has_value(),
              "stage_route_for should provide a direct stage lookup helper for llm consumers");
  assert_equal(std::string("lan.general"), *planner_route->fallback_route,
               "stage route lookup should preserve the model-profile fallback route");
  assert_true(!config->stage_route_for("unknown").has_value(),
              "stage_route_for should return empty when a stage is absent from the projected profile map");
}

void test_projector_rejects_invalid_overlay_without_rebuilding_parallel_config_system() {
  using dasall::llm::LLMSubsystemConfigOverlay;
  using dasall::llm::project_llm_subsystem_config;
  using dasall::tests::support::assert_true;

  LLMSubsystemConfigOverlay invalid_overlay;
  invalid_overlay.prompt_asset_sources.snapshot_cache_root = "runtime/cache/prompts";

  const auto config = project_llm_subsystem_config(make_runtime_policy_snapshot(), invalid_overlay);

  assert_true(!config.has_value(),
              "projector should reject inconsistent llm-local overlay input instead of silently inventing a second config system");
}

}  // namespace

int main() {
  try {
    test_projector_maps_runtime_policy_snapshot_into_llm_consumer_view();
    test_projector_applies_module_local_overlay_inputs();
    test_projector_rejects_invalid_overlay_without_rebuilding_parallel_config_system();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}