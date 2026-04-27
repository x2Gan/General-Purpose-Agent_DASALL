#include <exception>
#include <iostream>
#include <string>

#include "reasoning/Reasoner.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::CognitionConfig;
using dasall::cognition::ReasoningRequest;
using dasall::cognition::decision::ActionDecisionKind;
using dasall::cognition::perception::EntityCandidate;
using dasall::cognition::reasoning::Reasoner;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] CognitionConfig make_config() {
  return CognitionConfig{};
}

[[nodiscard]] ReasoningRequest make_execute_request() {
  ReasoningRequest request;
  request.caller_domain = "runtime.agent_orchestrator";
  request.request_id = "req-016-execute";
  request.trace_id = "trace-016-execute";
  request.profile_id = "desktop_full";

  request.goal_contract.goal_id = std::string("goal-016-execute");
  request.goal_contract.request_id = std::string("req-016-execute");
  request.goal_contract.goal_description =
      std::string("collect verifiable evidence for quarterly sales in Berlin");
  request.goal_contract.success_criteria =
      std::string("return evidence-backed quarterly sales findings for Berlin");
  request.goal_contract.status = dasall::contracts::GoalStatus::Active;
  request.goal_contract.created_at = 1712345600000;

  request.context_packet.request_id = std::string("req-016-execute");
  request.context_packet.user_turn =
      std::string("Find quarterly sales evidence for Berlin and summarize it");
  request.context_packet.current_goal_summary =
      std::string("find quarterly sales evidence for Berlin");
  request.context_packet.active_tools =
      std::vector<std::string>{std::string("agent.dataset")};

  request.belief_state.request_id = std::string("req-016-execute");
  request.belief_state.confirmed_facts =
      std::vector<std::string>{std::string("Berlin is the requested city")};
  request.belief_state.hypotheses =
      std::vector<std::string>{std::string("dataset contains quarterly sales")};
  request.belief_state.assumptions =
      std::vector<std::string>{std::string("dataset access is available")};
  request.belief_state.evidence_refs =
      std::vector<std::string>{std::string("belief:evidence:reasoner")};
  request.belief_state.confidence = 0.84F;

  request.perception_result.intent_summary =
      std::string("collect evidence for quarterly sales in Berlin");
  request.perception_result.task_type = std::string("action_decision");
  request.perception_result.entities = {
      EntityCandidate{.name = "tool",
                      .value = "agent.dataset",
                      .confidence = 0.95F,
                      .evidence_refs = {std::string("tool:agent.dataset")}},
  };
  request.perception_result.confidence = 0.82F;

  request.active_plan.plan_id = std::string("plan-016-execute");
  request.active_plan.revision = 1U;
  request.active_plan.nodes = {
      {.node_id = "plan-016-node-1",
       .objective = "Use agent.dataset to gather quarterly sales evidence for Berlin",
       .success_signal = "evidence for Berlin quarterly sales is gathered",
       .action_kind_hint = "tool_execution",
       .depends_on = {},
       .evidence_refs = {std::string("belief:evidence:reasoner")}},
      {.node_id = "plan-016-node-2",
       .objective = "Validate gathered evidence against the goal criteria",
       .success_signal = "evidence-backed quarterly sales findings are ready",
       .action_kind_hint = "validation",
       .depends_on = {std::string("plan-016-node-1")},
       .evidence_refs = {std::string("belief:evidence:reasoner")}},
  };
  request.active_plan.edges = {
      {.from_node_id = "plan-016-node-1",
       .to_node_id = "plan-016-node-2",
       .condition = "on_success",
       .evidence_refs = {std::string("belief:evidence:reasoner")}},
  };
  request.active_plan.plan_rationale = std::string("planner built an actionable evidence path");
  request.active_plan.estimated_complexity = 2U;

  return request;
}

void test_reasoner_selects_execute_action_for_actionable_plan() {
  Reasoner reasoner(make_config());

  const auto decision = reasoner.decide(make_execute_request());

  assert_true(decision.decision_kind == ActionDecisionKind::ExecuteAction,
              "actionable plan should project to ExecuteAction");
  assert_true(decision.selected_node_id.has_value(),
              "execute action should identify the selected node");
  assert_equal(std::string("plan-016-node-1"), *decision.selected_node_id,
               "reasoner should select the first actionable plan node");
  assert_true(decision.tool_intent_hint.has_value(),
              "execute action should expose a tool intent hint for runtime routing");
  assert_equal(std::string("agent.dataset"), decision.tool_intent_hint->tool_name,
               "tool intent hint should preserve the selected tool name");
  assert_true(decision.response_outline.has_value(),
              "execute action should still carry a response outline for downstream traces");
  assert_equal(4, static_cast<int>(decision.candidate_scores.size()),
               "reasoner should emit four candidate scores for explainability");
}

void test_reasoner_selects_direct_response_when_plan_is_terminal() {
  Reasoner reasoner(make_config());
  auto request = make_execute_request();
  request.perception_result.task_type = std::string("direct_response");
  request.active_plan.nodes = {
      {.node_id = "plan-016-direct",
       .objective = "Respond directly with the current evidence summary",
       .success_signal = "user receives a direct answer",
       .action_kind_hint = "direct_response",
       .depends_on = {},
       .evidence_refs = {std::string("belief:evidence:reasoner")}},
  };
  request.active_plan.edges.clear();
  request.active_plan.estimated_complexity = 1U;

  const auto decision = reasoner.decide(request);

  assert_true(decision.decision_kind == ActionDecisionKind::DirectResponse,
              "terminal direct-response plans should project to DirectResponse");
  assert_true(decision.response_outline.has_value(),
              "direct response should expose a response outline");
}

}  // namespace

int main() {
  try {
    test_reasoner_selects_execute_action_for_actionable_plan();
    test_reasoner_selects_direct_response_when_plan_is_terminal();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}