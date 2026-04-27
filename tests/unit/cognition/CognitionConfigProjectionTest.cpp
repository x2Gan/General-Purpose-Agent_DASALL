#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include "RuntimePolicySnapshot.h"
#include "config/CognitionConfigProjector.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::ModelCapabilityTier;
using dasall::cognition::config::CognitionConfigProjector;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::profiles::RuntimePolicySnapshot make_runtime_policy_snapshot(
    std::string profile_id = "desktop_full",
    bool allow_budget_degrade = true,
    double trace_sample_ratio = 0.25,
    bool omit_response_stage = false) {
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

  ModelProfile model_profile{
      .stage_routes = {
          {"planning",
           ModelRoutePolicy{.route = "llm.plan.primary",
                            .fallback_route = "llm.plan.fallback",
                            .streaming_enabled = false}},
          {"execution",
           ModelRoutePolicy{.route = "llm.exec.primary",
                            .fallback_route = "llm.exec.fallback",
                            .streaming_enabled = false}},
          {"reflection",
           ModelRoutePolicy{.route = "llm.reflect.primary",
                            .fallback_route = "llm.reflect.fallback",
                            .streaming_enabled = false}},
      },
  };

  if (!omit_response_stage) {
    model_profile.stage_routes.emplace(
        "response",
        ModelRoutePolicy{.route = "llm.response.primary",
                         .fallback_route = "llm.response.fallback",
                         .streaming_enabled = true});
  }

  return RuntimePolicySnapshot{
      42U,
      std::move(profile_id),
      dasall::contracts::RuntimeBudget{.max_tokens = 4096U,
                                       .max_turns = 12U,
                                       .max_tool_calls = 4U,
                                       .max_latency_ms = 2400U,
                                       .max_replan_count = 2U},
      std::move(model_profile),
      TokenBudgetPolicy{.max_input_tokens = 2048U,
                        .max_output_tokens = 768U,
                        .max_history_turns = 8U,
                        .compression_threshold = 1536U},
      PromptPolicy{.allowed_prompt_releases = {"stable"},
                   .trusted_sources = {"profiles"},
                   .tool_visibility_rules = {"builtin:all"}},
      CapabilityCachePolicy{.refresh_interval_ms = 30000,
                            .expire_after_ms = 120000,
                            .stale_read_allowed = false,
                            .failure_backoff_ms = 1000},
      DegradePolicy{.fallback_chain = {"template"},
                    .allow_model_failover = true,
                    .allow_budget_degrade = allow_budget_degrade},
      TimeoutPolicy{.llm = TimeoutBudget{.timeout_ms = 1800,
                                         .retry_budget = 1U,
                                         .circuit_breaker_threshold = 3U},
                    .tool = TimeoutBudget{.timeout_ms = 600,
                                          .retry_budget = 1U,
                                          .circuit_breaker_threshold = 2U},
                    .mcp = TimeoutBudget{.timeout_ms = 600,
                                         .retry_budget = 1U,
                                         .circuit_breaker_threshold = 2U},
                    .workflow = TimeoutBudget{.timeout_ms = 1500,
                                              .retry_budget = 1U,
                                              .circuit_breaker_threshold = 2U}},
      ExecutionPolicy{.requires_high_risk_confirmation = true,
                      .safe_mode_enabled = true,
                      .audit_level = "full",
                      .allowed_tool_domains = {"builtin"}},
      OpsPolicy{.log_level = "info",
                .metrics_granularity = "core",
                .trace_sample_ratio = trace_sample_ratio,
                .remote_diagnostics_enabled = false,
                .upgrade_strategy = "rolling"},
      4U,
  };
}

void test_project_config_uses_runtime_policy_snapshot_as_single_source_of_truth() {
  const auto config = CognitionConfigProjector::project_config(
      make_runtime_policy_snapshot("edge_minimal", true, 0.10));

  assert_true(config.has_value(),
              "consistent runtime policy snapshot should project a cognition config");
  assert_true(config->enabled,
              "cognition must remain enabled for every supported profile");
  assert_equal(4, static_cast<int>(config->max_plan_nodes),
               "edge_minimal should tighten the plan node cap instead of disabling cognition");
  assert_equal(2, static_cast<int>(config->max_plan_depth),
               "edge_minimal should tighten planning depth for the lightweight path");
  assert_true(config->perception.rule_fallback_enabled,
              "degrade policy should project directly into perception rule fallback enablement");
  assert_true(config->response.template_fallback_enabled,
              "degrade policy should project directly into response template fallback enablement");
  assert_true(!config->reasoner.allow_delegate_hint,
              "delegate hint should remain closed on the frozen first cognition surface");
  assert_true(config->observability.emit_stage_spans,
              "non-zero trace sampling should keep stage spans enabled");
  assert_true(config->observability.redact_context_payload,
              "context redaction must stay hard-enabled by the projector");
}

void test_derive_stage_model_hint_preserves_canonical_stage_keys_and_profile_shape() {
  const auto hint = CognitionConfigProjector::derive_stage_model_hint(
      make_runtime_policy_snapshot("cloud_full"), "planning", "replan");

  assert_true(hint.has_value(),
              "canonical stage name plus known task type should project a stage model hint");
  assert_equal(std::string("planning"), hint->stage_name,
               "projector must keep the canonical planning stage key unchanged");
  assert_equal(std::string("replan"), hint->task_type,
               "projector must preserve cognition semantics in task_type rather than inventing another stage vocabulary");
  assert_true(hint->capability_tier == ModelCapabilityTier::Advanced,
              "cloud_full replan should prefer the advanced planning tier");
  assert_true(hint->requires_structured_output,
              "planning stages should require structured output");
  assert_true(hint->requires_reasoning_trace,
              "advanced planning hints should request reasoning trace");
  assert_equal(768, static_cast<int>(hint->max_output_tokens),
               "max output tokens should project from the token budget policy");
  assert_equal(1800, static_cast<int>(hint->deadline_ms),
               "deadline should project from the llm timeout lane");
  assert_equal(std::string("llm.plan.primary"), hint->preferred_provider,
               "preferred provider should carry the canonical planning route from the runtime policy snapshot");
}

void test_derive_stage_model_hint_handles_response_and_reflection_defaults() {
  const auto response_hint = CognitionConfigProjector::derive_stage_model_hint(
      make_runtime_policy_snapshot("factory_test"), "response", "final_response");
  const auto reflection_hint = CognitionConfigProjector::derive_stage_model_hint(
      make_runtime_policy_snapshot("desktop_full"), "reflection", "failure_analysis");

  assert_true(response_hint.has_value(),
              "response stage should remain a legal canonical projection target");
  assert_true(!response_hint->requires_structured_output,
              "response stage should preserve the natural-language envelope default");
  assert_true(response_hint->capability_tier == ModelCapabilityTier::Standard,
              "factory_test response should keep the standard tier by default");
  assert_true(reflection_hint.has_value(),
              "reflection stage should remain a legal canonical projection target");
  assert_true(reflection_hint->capability_tier == ModelCapabilityTier::ReasoningHeavy,
              "desktop_full reflection should request the reasoning-heavy tier");
  assert_true(reflection_hint->requires_reasoning_trace,
              "reflection stage should always request reasoning trace");
}

void test_projector_rejects_missing_routes_and_noncanonical_stage_names() {
  const auto missing_route_config = CognitionConfigProjector::project_config(
      make_runtime_policy_snapshot("desktop_full", true, 0.25, true));
  const auto legacy_hint = CognitionConfigProjector::derive_stage_model_hint(
      make_runtime_policy_snapshot("desktop_full"), "reasoning", "action_decision");

  assert_true(!missing_route_config.has_value(),
              "projector should fail closed when required canonical stage routes are missing");
  assert_true(!legacy_hint.has_value(),
              "projector should reject noncanonical stage names instead of reviving private alias maps");
}

}  // namespace

int main() {
  try {
    test_project_config_uses_runtime_policy_snapshot_as_single_source_of_truth();
    test_derive_stage_model_hint_preserves_canonical_stage_keys_and_profile_shape();
    test_derive_stage_model_hint_handles_response_and_reflection_defaults();
    test_projector_rejects_missing_routes_and_noncanonical_stage_names();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}