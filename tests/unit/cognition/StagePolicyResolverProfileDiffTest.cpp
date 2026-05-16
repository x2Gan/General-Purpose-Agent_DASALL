#include <exception>
#include <iostream>
#include <string>

#include "RuntimePolicySnapshot.h"
#include "StagePolicyResolver.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::CognitionStepRequest;
using dasall::cognition::ModelCapabilityTier;
using dasall::cognition::ResponseBuildHints;
using dasall::cognition::ResponseBuildRequest;
using dasall::cognition::StageExecutionHints;
using dasall::cognition::policy::StageFallbackMode;
using dasall::cognition::policy::StagePolicyResolver;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::profiles::RuntimePolicySnapshot make_runtime_policy_snapshot(
    std::string profile_id,
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
      78U,
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
                    .allow_budget_degrade = true},
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
                .trace_sample_ratio = 0.25,
                .remote_diagnostics_enabled = false,
                .upgrade_strategy = "rolling"},
      4U,
  };
}

[[nodiscard]] CognitionStepRequest make_step_request(std::string profile_id) {
  return CognitionStepRequest{
      .caller_domain = "runtime",
      .request_id = "req-profile-diff",
      .trace_id = "trace-profile-diff",
      .profile_id = std::move(profile_id),
      .goal_contract = {},
      .context_packet = {},
      .belief_state = {},
      .latest_observation = std::nullopt,
      .budget_context = std::nullopt,
      .execution_hints = StageExecutionHints{},
  };
}

[[nodiscard]] ResponseBuildRequest make_response_request() {
  return ResponseBuildRequest{
      .caller_domain = "runtime",
      .request_id = "resp-profile-diff",
      .trace_id = "trace-resp-profile-diff",
      .profile_id = "factory_test",
      .goal_contract = {},
      .context_packet = {},
      .belief_state = std::nullopt,
      .latest_observation = std::nullopt,
      .terminal_decision = std::nullopt,
      .build_hints = ResponseBuildHints{.prefer_template = false,
                                        .allow_template_fallback = true,
                                        .max_summary_chars = 0U,
                                        .required_sections = {}},
  };
}

void test_profile_diff_changes_planning_cap_and_model_tier() {
  const auto edge_plan = StagePolicyResolver::resolve_decide_plan(
      make_runtime_policy_snapshot("edge_minimal"), make_step_request("edge_minimal"));
  const auto cloud_plan = StagePolicyResolver::resolve_decide_plan(
      make_runtime_policy_snapshot("cloud_full"), make_step_request("cloud_full"));

  assert_true(edge_plan.has_value(), "edge_minimal should still resolve a decision plan");
  assert_true(cloud_plan.has_value(), "cloud_full should still resolve a decision plan");
  assert_equal(4, static_cast<int>(edge_plan->max_plan_nodes),
               "edge_minimal should tighten max_plan_nodes instead of disabling cognition");
  assert_equal(8, static_cast<int>(cloud_plan->max_plan_nodes),
               "cloud_full should retain the full planning cap");
  assert_true(edge_plan->preferred_model_tier == ModelCapabilityTier::Standard,
              "edge_minimal should keep the lighter planning tier");
  assert_true(cloud_plan->preferred_model_tier == ModelCapabilityTier::Advanced,
              "cloud_full should preserve the advanced planning tier");
  assert_true(edge_plan->rule_fallback_enabled,
              "edge_minimal should preserve explicit local fallback as an allowed degraded path");
  assert_true(!cloud_plan->rule_fallback_enabled,
              "cloud_full should keep structured planning/execution on the fail-fast path");
}

void test_factory_test_prefers_template_mode_while_cloud_full_only_allows_it() {
  const auto factory_plan = StagePolicyResolver::resolve_response_plan(
      make_runtime_policy_snapshot("factory_test"), make_response_request());
  const auto cloud_plan = StagePolicyResolver::resolve_response_plan(
      make_runtime_policy_snapshot("cloud_full"), make_response_request());

  assert_true(factory_plan.has_value(), "factory_test should resolve a response stage plan");
  assert_true(cloud_plan.has_value(), "cloud_full should resolve a response stage plan");
  assert_true(factory_plan->fallback_mode == StageFallbackMode::TemplatePreferred,
              "factory_test should bias response fallback toward template-first mode");
  assert_true(cloud_plan->fallback_mode == StageFallbackMode::TemplateAllowed,
              "cloud_full should only expose template fallback as an allowed degraded path");
}

void test_missing_canonical_route_fails_closed() {
  const auto response_plan = StagePolicyResolver::resolve_response_plan(
      make_runtime_policy_snapshot("desktop_full", true), make_response_request());

  assert_true(!response_plan.has_value(),
              "resolver should fail closed when projector input is missing the canonical response route");
}

}  // namespace

int main() {
  try {
    test_profile_diff_changes_planning_cap_and_model_tier();
    test_factory_test_prefers_template_mode_while_cloud_full_only_allows_it();
    test_missing_canonical_route_fails_closed();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}