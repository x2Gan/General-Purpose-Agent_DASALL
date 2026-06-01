#include <exception>
#include <iostream>
#include <string>

#include "RuntimePolicySnapshot.h"
#include "StagePolicyResolver.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::BudgetContext;
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
    std::string profile_id = "desktop_full") {
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
      77U,
      std::move(profile_id),
      dasall::contracts::RuntimeBudget{.max_tokens = 4096U,
                                       .max_turns = 12U,
                                       .max_tool_calls = 4U,
                                       .max_latency_ms = 2400U,
                                       .max_replan_count = 2U},
      ModelProfile{.stage_routes = {
                       {"perception",
                        ModelRoutePolicy{.route = "llm.perception.primary",
                                         .fallback_route = "llm.perception.fallback",
                                         .streaming_enabled = false}},
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

[[nodiscard]] CognitionStepRequest make_step_request() {
  return CognitionStepRequest{
      .caller_domain = "runtime",
      .request_id = "req-012",
      .trace_id = "trace-012",
      .profile_id = "desktop_full",
      .goal_contract = {},
      .context_packet = {},
      .belief_state = {},
      .latest_observation = std::nullopt,
      .budget_context = BudgetContext{.total_budget_tokens = 1000U,
                                      .consumed_tokens = 200U,
                                      .remaining_tokens = 800U,
                                      .budget_utilization = 0.20F,
                                      .context_was_truncated = false,
                                      .near_budget_limit = false},
      .execution_hints = StageExecutionHints{},
  };
}

[[nodiscard]] ResponseBuildRequest make_response_request(bool prefer_template = false) {
  return ResponseBuildRequest{
      .caller_domain = "runtime",
      .request_id = "resp-012",
      .trace_id = "trace-resp-012",
      .profile_id = "factory_test",
      .goal_contract = {},
      .context_packet = {},
      .belief_state = std::nullopt,
      .latest_observation = std::nullopt,
      .terminal_decision = std::nullopt,
      .build_hints = ResponseBuildHints{.prefer_template = prefer_template,
                                        .allow_template_fallback = true,
                                        .max_summary_chars = 0U,
                                        .required_sections = {}},
  };
}

void test_resolve_decide_plan_uses_projected_defaults() {
  const auto plan =
      StagePolicyResolver::resolve_decide_plan(make_runtime_policy_snapshot(), make_step_request());

  assert_true(plan.has_value(),
              "desktop_full should resolve a decision plan from the projected cognition config");
  assert_equal(3, static_cast<int>(plan->enabled_stages.size()),
               "desktop_full should keep perception, planning and execution stages enabled");
  assert_equal(std::string("perception"), plan->enabled_stages.front(),
               "perception must become the first stage in the decision chain once canonicalized");
  assert_equal(std::string("planning"), plan->enabled_stages.at(1),
               "planning must remain the second stage in the decision chain");
  assert_equal(std::string("execution"), plan->enabled_stages.back(),
               "execution must remain the terminal decision stage");
  assert_true(plan->perception_llm_enabled,
              "desktop_full should expose perception llm classification in the stage execution plan");
  assert_true(plan->preferred_model_tier == ModelCapabilityTier::Advanced,
              "desktop_full planning path should prefer the advanced tier");
  assert_equal(8, static_cast<int>(plan->max_plan_nodes),
               "desktop_full should keep the default plan node cap");
  assert_equal(4, static_cast<int>(plan->max_plan_depth),
               "desktop_full should keep the default plan depth cap");
  assert_equal(1800, static_cast<int>(plan->deadline_ms),
               "stage deadline should default to the llm timeout lane when low latency is not requested");
  assert_true(plan->clarification_threshold == 0.45F,
              "clarification threshold should come from projected cognition defaults");
  assert_true(plan->fallback_mode == StageFallbackMode::None,
              "decision plan should not activate a fallback mode under healthy budget conditions");
  assert_true(!plan->rule_fallback_enabled,
              "desktop_full should keep structured planning/execution on the fail-fast path");
}

void test_resolve_response_plan_honors_template_preference() {
  const auto plan = StagePolicyResolver::resolve_response_plan(
      make_runtime_policy_snapshot("factory_test"), make_response_request(true));

  assert_true(plan.has_value(),
              "factory_test response path should resolve a response stage plan");
  assert_equal(1, static_cast<int>(plan->enabled_stages.size()),
               "response plan should only schedule the response stage");
  assert_equal(std::string("response"), plan->enabled_stages.front(),
               "response stage key must stay canonical");
  assert_true(plan->template_fallback_enabled,
              "response plan should expose template fallback when both config and request allow it");
  assert_true(plan->fallback_mode == StageFallbackMode::TemplatePreferred,
              "factory_test plus explicit template preference should select template-preferred mode");
}

}  // namespace

int main() {
  try {
    test_resolve_decide_plan_uses_projected_defaults();
    test_resolve_response_plan_honors_template_preference();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}