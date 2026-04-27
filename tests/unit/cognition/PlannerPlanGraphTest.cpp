#include <exception>
#include <iostream>
#include <string>

#include "planning/Planner.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::CognitionConfig;
using dasall::cognition::PlanningRequest;
using dasall::cognition::perception::ClarificationCandidate;
using dasall::cognition::perception::EntityCandidate;
using dasall::cognition::perception::PerceptionResult;
using dasall::cognition::planning::Planner;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] CognitionConfig make_config(std::uint32_t max_plan_nodes,
                                                                                    std::uint32_t max_plan_depth) {
    CognitionConfig config;
    config.max_plan_nodes = max_plan_nodes;
    config.max_plan_depth = max_plan_depth;
    return config;
}

[[nodiscard]] PlanningRequest make_planning_request(bool clarification_required = false) {
  PlanningRequest request;
  request.caller_domain = "runtime.agent_orchestrator";
  request.request_id = "req-015";
  request.trace_id = "trace-015";
  request.profile_id = "desktop_full";

  request.goal_contract.goal_id = std::string("goal-015");
  request.goal_contract.request_id = std::string("req-015");
  request.goal_contract.goal_description =
      std::string("collect verifiable evidence for quarterly sales in Berlin");
  request.goal_contract.success_criteria =
      std::string("return evidence-backed quarterly sales findings for Berlin");
  request.goal_contract.status = dasall::contracts::GoalStatus::Active;
  request.goal_contract.created_at = 1712345600000;
  request.goal_contract.constraints = std::string("stay within builtin tools");

  request.context_packet.request_id = std::string("req-015");
  request.context_packet.user_turn =
      std::string("Find quarterly sales evidence for Berlin and summarize it");
  request.context_packet.current_goal_summary =
      std::string("find quarterly sales evidence for Berlin");
  request.context_packet.recent_history =
      std::vector<std::string>{std::string("user asked for a city-specific evidence summary")};
  request.context_packet.active_tools =
      std::vector<std::string>{std::string("agent.dataset")};
  request.context_packet.policy_digest = std::string("builtin-only policy");

  request.belief_state.request_id = std::string("req-015");
  request.belief_state.confirmed_facts =
      std::vector<std::string>{std::string("Berlin is the requested city")};
  request.belief_state.hypotheses =
      std::vector<std::string>{std::string("the builtin dataset contains quarterly sales")};
  request.belief_state.assumptions =
      std::vector<std::string>{std::string("existing sales data is recent enough")};
  request.belief_state.evidence_refs =
      std::vector<std::string>{std::string("belief:evidence:berlin")};
  request.belief_state.confidence = 0.85F;
  request.belief_state.goal_id = std::string("goal-015");
  request.belief_state.created_at = 1712345600100;

  request.perception_result.intent_summary =
      std::string("collect evidence for quarterly sales in Berlin");
  request.perception_result.task_type = std::string("action_decision");
  request.perception_result.entities = {
      EntityCandidate{.name = "tool",
                      .value = "agent.dataset",
                      .confidence = 0.95F,
                      .evidence_refs = {std::string("tool:agent.dataset")}},
  };
  request.perception_result.constraints_digest.hard_constraints =
      std::vector<std::string>{std::string("stay within builtin tools")};
  request.perception_result.constraints_digest.policy_refs =
      std::vector<std::string>{std::string("builtin-only policy")};
  request.perception_result.confidence = clarification_required ? 0.30F : 0.86F;
  request.perception_result.requires_clarification = clarification_required;
  if (clarification_required) {
    request.perception_result.clarification_questions = {
        ClarificationCandidate{.question = "Which quarter should the planner focus on?",
                               .evidence_refs = {std::string("belief:evidence:berlin")},
                               .priority = 0.9F},
    };
  }

  return request;
}

void test_build_plan_expands_goal_into_a_valid_dag() {
    Planner planner(make_config(6U, 6U));

  const auto graph = planner.build_plan(make_planning_request(false));

  assert_equal(std::string("plan-req-015"), graph.plan_id,
               "planner should derive a stable plan id from the request id");
  assert_equal(1, static_cast<int>(graph.revision),
               "newly built plans should start at revision 1");
  assert_equal(4, static_cast<int>(graph.nodes.size()),
               "actionable planning input with a hypothesis should expand into four staged nodes");
  assert_equal(3, static_cast<int>(graph.edges.size()),
               "the staged plan should expose a linear DAG with three edges");
  assert_true(graph.open_questions.empty(),
              "fully actionable input should not emit blocking open questions");
  assert_equal(std::string("validation"), graph.nodes.back().action_kind_hint,
               "the final node should remain an explicit validation node");
}

void test_build_plan_prefers_open_questions_when_clarification_is_required() {
  Planner planner(CognitionConfig{});

  const auto graph = planner.build_plan(make_planning_request(true));

  assert_true(graph.nodes.empty(),
              "clarification-required input should not force a pseudo DAG");
  assert_equal(1, static_cast<int>(graph.open_questions.size()),
               "clarification-required input should surface an open question");
  assert_equal(std::string("Which quarter should the planner focus on?"),
               graph.open_questions.front().question,
               "planner should preserve the clarification question text from perception");
}

}  // namespace

int main() {
  try {
    test_build_plan_expands_goal_into_a_valid_dag();
    test_build_plan_prefers_open_questions_when_clarification_is_required();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}