#include <cmath>
#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include "RuntimePolicySnapshot.h"
#include "config/CognitionConfigProjector.h"
#include "reasoning/Reasoner.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::CognitionConfig;
using dasall::cognition::ReasoningRequest;
using dasall::cognition::config::CognitionConfigProjector;
using dasall::cognition::decision::ActionDecision;
using dasall::cognition::decision::ActionDecisionKind;
using dasall::cognition::perception::EntityCandidate;
using dasall::cognition::reasoning::Reasoner;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::profiles::RuntimePolicySnapshot make_runtime_policy_snapshot(
    std::string profile_id) {
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
      42U,
      std::move(profile_id),
      dasall::contracts::RuntimeBudget{.max_tokens = 4096U,
                                       .max_turns = 12U,
                                       .max_tool_calls = 4U,
                                       .max_latency_ms = 2400U,
                                       .max_replan_count = 2U},
      ModelProfile{.stage_routes = {{"planning",
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
                                                      .streaming_enabled = true}}}},
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

void assert_close(float actual, float expected, const char* message) {
  assert_true(std::fabs(actual - expected) < 0.001F, message);
}

[[nodiscard]] float lookup_candidate_score(const ActionDecision& decision,
                                           std::string_view candidate_name) {
  for (const auto& candidate : decision.candidate_scores) {
    if (candidate.candidate_name == candidate_name) {
      return candidate.score;
    }
  }

  return -1.0F;
}

[[nodiscard]] ReasoningRequest make_weight_sensitive_request() {
  ReasoningRequest request;
  request.caller_domain = "runtime.agent_orchestrator";
  request.request_id = "req-gap-009-weights";
  request.trace_id = "trace-gap-009-weights";
  request.profile_id = "desktop_full";

  request.goal_contract.goal_id = std::string("goal-gap-009-weights");
  request.goal_contract.request_id = std::string("req-gap-009-weights");
  request.goal_contract.goal_description =
      std::string("decide whether to answer directly or execute a supporting tool step");
  request.goal_contract.success_criteria =
      std::string("either produce a safe direct answer or run the first tool step");
  request.goal_contract.status = dasall::contracts::GoalStatus::Active;
  request.goal_contract.created_at = 1712345600000;

  request.context_packet.request_id = std::string("req-gap-009-weights");
  request.context_packet.user_turn =
      std::string("Summarize the likely answer, but use a tool if more evidence is needed");
  request.context_packet.current_goal_summary =
      std::string("balance direct response against tool-backed evidence collection");
  request.context_packet.active_tools =
      std::vector<std::string>{std::string("agent.dataset")};

  request.belief_state.request_id = std::string("req-gap-009-weights");
  request.belief_state.confirmed_facts =
      std::vector<std::string>{std::string("tool access is available")};
  request.belief_state.hypotheses =
      std::vector<std::string>{std::string("enough evidence may already exist for a direct answer")};
  request.belief_state.assumptions =
      std::vector<std::string>{std::string("fresh tool evidence would improve confidence")};
  request.belief_state.evidence_refs =
      std::vector<std::string>{std::string("belief:evidence:gap-009")};
  request.belief_state.confidence = 0.90F;

  request.perception_result.intent_summary =
      std::string("compare direct response against a lightweight evidence-gathering action");
  request.perception_result.task_type = std::string("direct_response");
  request.perception_result.entities = {
      EntityCandidate{.name = "tool",
                      .value = "agent.dataset",
                      .confidence = 0.95F,
                      .evidence_refs = {std::string("tool:agent.dataset")}},
  };
  request.perception_result.confidence = 0.80F;

  request.active_plan.plan_id = std::string("plan-gap-009-weights");
  request.active_plan.revision = 1U;
  request.active_plan.nodes = {
      {.node_id = "plan-gap-009-node-1",
       .objective = "Query the dataset once to confirm the current answer",
       .success_signal = "fresh evidence is available",
       .action_kind_hint = "tool_execution",
       .depends_on = {},
       .evidence_refs = {std::string("belief:evidence:gap-009")}},
      {.node_id = "plan-gap-009-node-2",
       .objective = "Return the answer once confidence is sufficient",
       .success_signal = "user receives the final answer",
       .action_kind_hint = "validation",
       .depends_on = {std::string("plan-gap-009-node-1")},
       .evidence_refs = {std::string("belief:evidence:gap-009")}},
  };
  request.active_plan.edges = {
      {.from_node_id = "plan-gap-009-node-1",
       .to_node_id = "plan-gap-009-node-2",
       .condition = "on_success",
       .evidence_refs = {std::string("belief:evidence:gap-009")}},
  };
  request.active_plan.plan_rationale =
      std::string("both a direct answer and a lightweight tool step remain plausible");
  request.active_plan.estimated_complexity = 2U;

  request.latest_observation = dasall::contracts::Observation{};
  request.latest_observation->observation_id = std::string("obs-gap-009-success");
  request.latest_observation->success = true;
  request.latest_observation->payload =
      std::string("latest evidence is consistent with the current hypothesis");
  request.latest_observation->created_at = 1712345602000;

  return request;
}

void test_projected_reasoner_candidate_weight_table_differs_by_profile() {
  const auto desktop_config =
      CognitionConfigProjector::project_config(make_runtime_policy_snapshot("desktop_full"));
  const auto edge_config =
      CognitionConfigProjector::project_config(make_runtime_policy_snapshot("edge_minimal"));

  assert_true(desktop_config.has_value(),
              "desktop_full runtime policy should project a cognition config");
  assert_true(edge_config.has_value(),
              "edge_minimal runtime policy should project a cognition config");
  assert_close(desktop_config->reasoner.candidate_weights.tool_call, 1.05F,
               "desktop_full should bias the reasoner toward tool-call execution");
  assert_close(desktop_config->reasoner.candidate_weights.direct_response, 0.95F,
               "desktop_full should slightly damp direct-response bias");
  assert_close(edge_config->reasoner.candidate_weights.tool_call, 0.90F,
               "edge_minimal should down-weight tool-call execution");
  assert_close(edge_config->reasoner.candidate_weights.direct_response, 1.10F,
               "edge_minimal should bias the reasoner toward direct response");
}

void test_same_request_routes_differently_under_projected_weights() {
  const auto desktop_config =
      CognitionConfigProjector::project_config(make_runtime_policy_snapshot("desktop_full"));
  const auto edge_config =
      CognitionConfigProjector::project_config(make_runtime_policy_snapshot("edge_minimal"));
  const auto request = make_weight_sensitive_request();

  assert_true(desktop_config.has_value() && edge_config.has_value(),
              "profile projection must succeed before reasoner weight comparison");

  const auto desktop_decision = Reasoner(*desktop_config).decide(request);
  const auto edge_decision = Reasoner(*edge_config).decide(request);

  assert_true(desktop_decision.decision_kind == ActionDecisionKind::ExecuteAction,
              "desktop_full weights should keep the same boundary request on the tool path");
  assert_true(edge_decision.decision_kind == ActionDecisionKind::DirectResponse,
              "edge_minimal weights should let the same request collapse to direct response");
  assert_true(lookup_candidate_score(desktop_decision, "execute_action") >
                  lookup_candidate_score(desktop_decision, "direct_response"),
              "desktop_full candidate weights should keep execute_action ahead of direct_response");
  assert_true(lookup_candidate_score(edge_decision, "direct_response") >
                  lookup_candidate_score(edge_decision, "execute_action"),
              "edge_minimal candidate weights should flip the same ranking toward direct_response");
}

}  // namespace

int main() {
  try {
    test_projected_reasoner_candidate_weight_table_differs_by_profile();
    test_same_request_routes_differently_under_projected_weights();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}