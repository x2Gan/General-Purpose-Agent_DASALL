#include <exception>
#include <iostream>

#include "RuntimePolicySnapshot.h"
#include "StagePolicyResolver.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::BudgetContext;
using dasall::cognition::CognitionStepRequest;
using dasall::cognition::StageExecutionHints;
using dasall::cognition::policy::StageFallbackMode;
using dasall::cognition::policy::StagePolicyResolver;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::profiles::RuntimePolicySnapshot make_runtime_policy_snapshot(
    std::string profile_id = "edge_balanced") {
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
      79U,
      std::move(profile_id),
      dasall::contracts::RuntimeBudget{.max_tokens = 4096U,
                                       .max_turns = 12U,
                                       .max_tool_calls = 4U,
                                       .max_latency_ms = 2400U,
                                       .max_replan_count = 2U},
      ModelProfile{.stage_routes = {
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
                       {"response",
                        ModelRoutePolicy{.route = "llm.response.primary",
                                         .fallback_route = "llm.response.fallback",
                                         .streaming_enabled = true}},
                   }},
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

[[nodiscard]] CognitionStepRequest make_step_request(float budget_utilization,
                                                     bool low_latency_preferred,
                                                     bool context_was_truncated,
                                                     bool near_budget_limit) {
  return CognitionStepRequest{
      .caller_domain = "runtime",
      .request_id = "budget-aware",
      .trace_id = "trace-budget-aware",
      .profile_id = "edge_balanced",
      .goal_contract = {},
      .context_packet = {},
      .belief_state = {},
      .latest_observation = std::nullopt,
      .budget_context = BudgetContext{.total_budget_tokens = 1000U,
                                      .consumed_tokens = static_cast<std::uint32_t>(budget_utilization * 1000.0F),
                                      .remaining_tokens = static_cast<std::uint32_t>((1.0F - budget_utilization) * 1000.0F),
                                      .budget_utilization = budget_utilization,
                                      .context_was_truncated = context_was_truncated,
                                      .near_budget_limit = near_budget_limit},
      .execution_hints = StageExecutionHints{.low_latency_preferred = low_latency_preferred,
                                             .degraded_path_allowed = true,
                                             .risk_tolerance = 0.0F,
                                             .profile_variant_hint = std::nullopt},
  };
}

void test_mid_budget_pressure_halves_plan_nodes_and_tightens_deadline() {
  const auto plan = StagePolicyResolver::resolve_decide_plan(
      make_runtime_policy_snapshot(), make_step_request(0.65F, true, false, false));

  assert_true(plan.has_value(), "mid-budget pressure should still keep a legal decision plan");
  assert_equal(3, static_cast<int>(plan->max_plan_nodes),
               "edge_balanced should halve plan nodes under mid-budget pressure");
  assert_equal(900, static_cast<int>(plan->deadline_ms),
               "low_latency_preferred should halve the llm deadline budget");
  assert_true(plan->clarification_threshold == 0.55F,
              "mid-budget pressure should raise clarification conservatism by 0.10");
  assert_true(plan->fallback_mode == StageFallbackMode::Conservative,
              "mid-budget pressure should activate conservative fallback mode");
}

void test_high_budget_pressure_clamps_plan_shape_and_truncation_raises_threshold() {
  const auto plan = StagePolicyResolver::resolve_decide_plan(
      make_runtime_policy_snapshot(), make_step_request(0.85F, false, true, true));

  assert_true(plan.has_value(), "high-budget pressure should still yield a conservative plan");
  assert_equal(2, static_cast<int>(plan->max_plan_nodes),
               "high-budget pressure should clamp plan nodes to the shallow 1-2 node band");
  assert_equal(2, static_cast<int>(plan->max_plan_depth),
               "high-budget pressure should clamp planning depth for shallow execution");
  assert_true(plan->clarification_threshold == 0.70F,
              "high-budget plus truncation should raise clarification threshold by 0.25 in total");
  assert_true(plan->degraded_mode_active,
              "high-budget pressure should explicitly mark degraded mode as active");
  assert_true(plan->fallback_mode == StageFallbackMode::Conservative,
              "near-budget-limit should keep the plan in conservative fallback mode");
}

}  // namespace

int main() {
  try {
    test_mid_budget_pressure_halves_plan_nodes_and_tightens_deadline();
    test_high_budget_pressure_clamps_plan_shape_and_truncation_raises_threshold();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}