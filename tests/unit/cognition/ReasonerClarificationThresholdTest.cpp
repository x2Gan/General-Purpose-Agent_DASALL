#include <exception>
#include <iostream>
#include <string>

#include "reasoning/Reasoner.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::CognitionConfig;
using dasall::cognition::ReasoningRequest;
using dasall::cognition::decision::ActionDecisionKind;
using dasall::cognition::perception::ClarificationCandidate;
using dasall::cognition::reasoning::Reasoner;
using dasall::tests::support::assert_true;

[[nodiscard]] ReasoningRequest make_request() {
  ReasoningRequest request;
  request.caller_domain = "runtime.agent_orchestrator";
  request.request_id = "req-016-clarify";
  request.trace_id = "trace-016-clarify";
  request.profile_id = "desktop_full";

  request.goal_contract.goal_id = std::string("goal-016-clarify");
  request.goal_contract.request_id = std::string("req-016-clarify");
  request.goal_contract.goal_description = std::string("resolve the current user ambiguity");
  request.goal_contract.success_criteria = std::string("the missing parameter is clarified");
  request.goal_contract.status = dasall::contracts::GoalStatus::Active;
  request.goal_contract.created_at = 1712345600000;

  request.context_packet.request_id = std::string("req-016-clarify");
  request.context_packet.user_turn = std::string("Help me with it maybe");
  request.context_packet.current_goal_summary = std::string("clarify the missing target");

  request.belief_state.request_id = std::string("req-016-clarify");
  request.belief_state.confirmed_facts = std::vector<std::string>{};
  request.belief_state.hypotheses = std::vector<std::string>{std::string("the user may mean the sales dashboard")};
  request.belief_state.assumptions = std::vector<std::string>{};
  request.belief_state.evidence_refs = std::vector<std::string>{std::string("belief:evidence:clarify")};
  request.belief_state.confidence = 0.30F;

  request.perception_result.intent_summary = std::string("clarify the missing target");
  request.perception_result.task_type = std::string("action_decision");
  request.perception_result.confidence = 0.28F;
  request.perception_result.requires_clarification = true;
  request.perception_result.clarification_questions = {
      ClarificationCandidate{.question = "Which target should the agent operate on?",
                             .evidence_refs = {std::string("belief:evidence:clarify")},
                             .priority = 0.95F},
  };

  request.active_plan.plan_id = std::string("plan-016-clarify");
  request.active_plan.revision = 1U;
  request.active_plan.open_questions = {
      {.question_id = "open-question-016",
       .question = "Which target should the agent operate on?",
       .reason = "current target is underspecified",
       .blocks_plan = true,
       .evidence_refs = {std::string("belief:evidence:clarify")}},
  };
  request.active_plan.plan_rationale = std::string("clarification required before execution");
  request.active_plan.estimated_complexity = 1U;

  return request;
}

void test_reasoner_prefers_ask_clarification_below_threshold() {
  Reasoner reasoner(CognitionConfig{});

  const auto decision = reasoner.decide(make_request());

  assert_true(decision.decision_kind == ActionDecisionKind::AskClarification,
              "low-confidence open-question inputs should project to AskClarification");
  assert_true(decision.clarification_needed,
              "clarification decisions must keep the clarification flag active");
  assert_true(decision.clarification_question.has_value() &&
                  *decision.clarification_question ==
                      "Which target should the agent operate on?",
              "reasoner should preserve the open question text");
  assert_true(decision.confidence >= 0.45F,
              "clarification path should score above the clarification threshold");
}

}  // namespace

int main() {
  try {
    test_reasoner_prefers_ask_clarification_below_threshold();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}