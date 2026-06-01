#include <exception>
#include <iostream>
#include <string_view>

#include "RuntimePolicySnapshot.h"
#include "StagePolicyResolver.h"
#include "reasoning/Reasoner.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::CognitionConfig;
using dasall::cognition::BudgetContext;
using dasall::cognition::CognitionStepRequest;
using dasall::cognition::ReasoningRequest;
using dasall::cognition::StageExecutionHints;
using dasall::cognition::decision::ActionDecisionKind;
using dasall::cognition::policy::StageFallbackMode;
using dasall::cognition::policy::StagePolicyResolver;
using dasall::cognition::reasoning::Reasoner;
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

[[nodiscard]] bool has_diagnostic(const std::vector<std::string>& diagnostics,
                                  std::string_view diagnostic) {
  for (const auto& value : diagnostics) {
    if (value == diagnostic) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] bool has_prefixed_diagnostic(
    const std::vector<std::string>& diagnostics,
    std::string_view prefix) {
  for (const auto& value : diagnostics) {
    if (value.rfind(prefix, 0U) == 0U) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] ReasoningRequest make_reasoning_request(
    float budget_utilization,
    std::string_view task_type,
    bool near_budget_limit = false) {
  ReasoningRequest request;
  request.caller_domain = "runtime.agent_orchestrator";
  request.request_id = "budget-aware-reasoner";
  request.trace_id = "trace-budget-aware-reasoner";
  request.profile_id = "edge_balanced";

  request.goal_contract.goal_id = std::string("goal-budget-aware");
  request.goal_contract.request_id = std::string("budget-aware-reasoner");
  request.goal_contract.goal_description =
      std::string("collect verifiable evidence for quarterly sales in Berlin");
  request.goal_contract.success_criteria =
      std::string("return evidence-backed quarterly sales findings for Berlin");
  request.goal_contract.status = dasall::contracts::GoalStatus::Active;
  request.goal_contract.created_at = 1712345600000;

  request.context_packet.request_id = std::string("budget-aware-reasoner");
  request.context_packet.user_turn =
      std::string("Find quarterly sales evidence for Berlin and summarize it");
  request.context_packet.current_goal_summary =
      std::string("find quarterly sales evidence for Berlin");
  request.context_packet.active_tools =
      std::vector<std::string>{std::string("agent.dataset")};

  request.belief_state.request_id = std::string("budget-aware-reasoner");
  request.belief_state.confirmed_facts =
      std::vector<std::string>{std::string("Berlin is the requested city")};
  request.belief_state.hypotheses =
      std::vector<std::string>{std::string("dataset contains quarterly sales")};
  request.belief_state.assumptions =
      std::vector<std::string>{std::string("dataset access is available")};
  request.belief_state.evidence_refs =
      std::vector<std::string>{std::string("belief:evidence:budget-aware")};
  request.belief_state.confidence = 0.90F;

  request.perception_result.intent_summary =
      std::string("collect evidence for quarterly sales in Berlin");
  request.perception_result.task_type = std::string(task_type);
  request.perception_result.confidence = 0.90F;

  request.active_plan.plan_id = std::string("plan-budget-aware");
  request.active_plan.revision = 1U;
  request.active_plan.nodes = {
      {.node_id = "plan-budget-aware-node-1",
       .objective = "Use agent.dataset to gather quarterly sales evidence for Berlin",
       .success_signal = "evidence for Berlin quarterly sales is gathered",
       .action_kind_hint = "tool_execution",
       .depends_on = {},
       .evidence_refs = {std::string("belief:evidence:budget-aware")}},
  };
  request.active_plan.plan_rationale =
      std::string("planner built an actionable evidence path");
  request.active_plan.estimated_complexity = 1U;

  request.budget_context = BudgetContext{.total_budget_tokens = 1000U,
                                         .consumed_tokens = static_cast<std::uint32_t>(budget_utilization * 1000.0F),
                                         .remaining_tokens = static_cast<std::uint32_t>((1.0F - budget_utilization) * 1000.0F),
                                         .budget_utilization = budget_utilization,
                                         .context_was_truncated = false,
                                         .near_budget_limit = near_budget_limit};

  return request;
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

void test_reasoner_high_budget_pressure_forces_explicit_converge_safe_branch() {
  Reasoner reasoner(CognitionConfig{});

  const auto decision =
      reasoner.decide(make_reasoning_request(0.85F, "action_decision"));

  assert_true(decision.decision_kind == ActionDecisionKind::ConvergeSafe,
              "budget utilization >= 0.8 should stop actionable requests from remaining on ExecuteAction");
  assert_true(has_diagnostic(decision.diagnostics,
                             "budget_pressure_decision_path:converge_safe"),
              "high-budget converge-safe decisions should record the explicit budget-pressure branch");
}

void test_reasoner_high_budget_pressure_keeps_direct_response_on_explicit_branch() {
  Reasoner reasoner(CognitionConfig{});

  const auto decision =
      reasoner.decide(make_reasoning_request(0.85F, "direct_response"));

  assert_true(decision.decision_kind == ActionDecisionKind::DirectResponse,
              "direct-response intents under high budget pressure should still use the explicit direct-response branch");
  assert_true(has_diagnostic(decision.diagnostics,
                             "budget_pressure_decision_path:direct_response"),
              "high-budget direct-response decisions should record the explicit budget-pressure branch");
}

void test_reasoner_sub_threshold_budget_pressure_does_not_emit_explicit_branch_diagnostic() {
  Reasoner reasoner(CognitionConfig{});

  const auto decision =
      reasoner.decide(make_reasoning_request(0.65F, "action_decision"));

  assert_true(decision.decision_kind == ActionDecisionKind::ExecuteAction,
              "sub-threshold budget pressure should keep the normal execute-action path when a strong actionable candidate exists");
  assert_true(!has_prefixed_diagnostic(decision.diagnostics,
                                       "budget_pressure_decision_path:"),
              "sub-threshold budget pressure should not emit the explicit high-budget branch diagnostic");
}

}  // namespace

int main() {
  try {
    test_mid_budget_pressure_halves_plan_nodes_and_tightens_deadline();
    test_high_budget_pressure_clamps_plan_shape_and_truncation_raises_threshold();
    test_reasoner_high_budget_pressure_forces_explicit_converge_safe_branch();
    test_reasoner_high_budget_pressure_keeps_direct_response_on_explicit_branch();
    test_reasoner_sub_threshold_budget_pressure_does_not_emit_explicit_branch_diagnostic();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}