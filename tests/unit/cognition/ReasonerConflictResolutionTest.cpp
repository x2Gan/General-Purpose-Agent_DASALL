#include <exception>
#include <iostream>
#include <string>

#include "reasoning/Reasoner.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::BudgetContext;
using dasall::cognition::CognitionConfig;
using dasall::cognition::ReasoningRequest;
using dasall::cognition::decision::ActionDecisionKind;
using dasall::cognition::reasoning::Reasoner;
using dasall::tests::support::assert_true;

[[nodiscard]] ReasoningRequest make_base_request() {
  ReasoningRequest request;
  request.caller_domain = "runtime.agent_orchestrator";
  request.request_id = "req-016-conflict";
  request.trace_id = "trace-016-conflict";
  request.profile_id = "desktop_full";

  request.goal_contract.goal_id = std::string("goal-016-conflict");
  request.goal_contract.request_id = std::string("req-016-conflict");
  request.goal_contract.goal_description =
      std::string("collect verifiable evidence for quarterly sales in Berlin");
  request.goal_contract.success_criteria =
      std::string("return evidence-backed quarterly sales findings for Berlin");
  request.goal_contract.status = dasall::contracts::GoalStatus::Active;
  request.goal_contract.created_at = 1712345600000;

  request.context_packet.request_id = std::string("req-016-conflict");
  request.context_packet.user_turn =
      std::string("Find quarterly sales evidence for Berlin and summarize it");
  request.context_packet.current_goal_summary =
      std::string("find quarterly sales evidence for Berlin");

  request.belief_state.request_id = std::string("req-016-conflict");
  request.belief_state.confirmed_facts =
      std::vector<std::string>{std::string("Berlin is the requested city")};
  request.belief_state.hypotheses =
      std::vector<std::string>{std::string("dataset contains quarterly sales")};
  request.belief_state.assumptions =
      std::vector<std::string>{std::string("dataset access is available")};
  request.belief_state.evidence_refs =
      std::vector<std::string>{std::string("belief:evidence:conflict")};
  request.belief_state.confidence = 0.80F;

  request.perception_result.intent_summary =
      std::string("collect evidence for quarterly sales in Berlin");
  request.perception_result.task_type = std::string("action_decision");
  request.perception_result.confidence = 0.82F;

  request.active_plan.plan_id = std::string("plan-016-conflict");
  request.active_plan.revision = 1U;
  request.active_plan.nodes = {
      {.node_id = "plan-016-conflict-node",
       .objective = "Use agent.dataset to gather quarterly sales evidence for Berlin",
       .success_signal = "evidence for Berlin quarterly sales is gathered",
       .action_kind_hint = "tool_execution",
       .depends_on = {},
       .evidence_refs = {std::string("belief:evidence:conflict")}},
  };
  request.active_plan.plan_rationale = std::string("planner built an actionable evidence path");
  request.active_plan.estimated_complexity = 1U;

  return request;
}

void test_reasoner_prefers_clarification_when_observation_conflicts_with_plan() {
  Reasoner reasoner(CognitionConfig{});
  auto request = make_base_request();
  request.latest_observation = dasall::contracts::Observation{};
  request.latest_observation->observation_id = std::string("obs-016-conflict");
  request.latest_observation->success = false;
  request.latest_observation->payload =
      std::string("observation contradicts the current plan assumptions");
  request.latest_observation->created_at = 1712345602000;

  const auto decision = reasoner.decide(request);

  assert_true(decision.decision_kind == ActionDecisionKind::AskClarification,
              "conflicting observations should bias the reasoner toward clarification");
  assert_true(decision.clarification_question.has_value(),
              "conflict resolution should surface a clarification question");
}

void test_reasoner_converges_safe_when_budget_is_exhausted_and_no_actionable_node_remains() {
  Reasoner reasoner(CognitionConfig{});
  auto request = make_base_request();
  request.active_plan.nodes.clear();
  request.active_plan.estimated_complexity = 0U;
  request.budget_context = BudgetContext{.total_budget_tokens = 1000U,
                                         .consumed_tokens = 910U,
                                         .remaining_tokens = 90U,
                                         .budget_utilization = 0.91F,
                                         .context_was_truncated = false,
                                         .near_budget_limit = true};

  const auto decision = reasoner.decide(request);

  assert_true(decision.decision_kind == ActionDecisionKind::ConvergeSafe,
              "budget-exhausted inputs without actionable nodes should converge safely");
  assert_true(decision.response_outline.has_value(),
              "converge-safe decisions should still preserve a response outline");
}

}  // namespace

int main() {
  try {
    test_reasoner_prefers_clarification_when_observation_conflicts_with_plan();
    test_reasoner_converges_safe_when_budget_is_exhausted_and_no_actionable_node_remains();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}