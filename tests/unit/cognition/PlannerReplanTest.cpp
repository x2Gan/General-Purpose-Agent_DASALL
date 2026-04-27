#include <exception>
#include <iostream>
#include <string>

#include "planning/Planner.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::CognitionConfig;
using dasall::cognition::PlanningRequest;
using dasall::cognition::ReplanRequest;
using dasall::cognition::perception::EntityCandidate;
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

[[nodiscard]] PlanningRequest make_planning_request() {
  PlanningRequest request;
  request.caller_domain = "runtime.agent_orchestrator";
  request.request_id = "req-015-replan";
  request.trace_id = "trace-015-replan";
  request.profile_id = "desktop_full";

  request.goal_contract.goal_id = std::string("goal-015-replan");
  request.goal_contract.request_id = std::string("req-015-replan");
  request.goal_contract.goal_description =
      std::string("collect verifiable evidence for quarterly sales in Berlin");
  request.goal_contract.success_criteria =
      std::string("return evidence-backed quarterly sales findings for Berlin");
  request.goal_contract.status = dasall::contracts::GoalStatus::Active;
  request.goal_contract.created_at = 1712345600000;

  request.context_packet.request_id = std::string("req-015-replan");
  request.context_packet.user_turn =
      std::string("Find quarterly sales evidence for Berlin and summarize it");
  request.context_packet.current_goal_summary =
      std::string("find quarterly sales evidence for Berlin");

  request.belief_state.request_id = std::string("req-015-replan");
  request.belief_state.confirmed_facts =
      std::vector<std::string>{std::string("Berlin is the requested city")};
  request.belief_state.hypotheses =
      std::vector<std::string>{std::string("the builtin dataset contains quarterly sales")};
  request.belief_state.assumptions =
      std::vector<std::string>{std::string("dataset access is available")};
  request.belief_state.evidence_refs =
      std::vector<std::string>{std::string("belief:evidence:replan")};
  request.belief_state.confidence = 0.82F;
  request.belief_state.goal_id = std::string("goal-015-replan");
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
  request.perception_result.confidence = 0.84F;

  return request;
}

void test_replan_preserves_plan_identity_and_increments_revision() {
    Planner planner(make_config(6U, 6U));
    const auto planning_request = make_planning_request();
    const auto active_plan = planner.build_plan(planning_request);

  ReplanRequest request;
  request.caller_domain = "runtime.agent_orchestrator";
  request.request_id = "req-015-replan";
  request.trace_id = "trace-015-replan";
  request.profile_id = "desktop_full";
    request.goal_contract = planning_request.goal_contract;
    request.context_packet = planning_request.context_packet;
    request.belief_state = planning_request.belief_state;
  request.active_plan = active_plan;
  request.latest_observation.observation_id = std::string("obs-015-replan");
  request.latest_observation.success = false;
  request.latest_observation.payload = std::string("dataset lookup returned incomplete evidence");
  request.latest_observation.created_at = 1712345602000;
  request.latest_observation.error = dasall::contracts::ErrorInfo{
      .failure_type = dasall::contracts::ResultCodeCategory::Tool,
      .retryable = true,
      .safe_to_replan = true,
      .details = {.code = 429,
                  .message = "dataset lookup returned incomplete evidence",
                  .stage = "planning"},
      .source_ref = {.ref_type = "observation", .ref_id = "obs-015-replan"},
  };

  const auto replan = planner.replan(request);

  assert_equal(active_plan.plan_id, replan.new_plan.plan_id,
               "replan should keep the existing plan id stable");
  assert_equal(static_cast<int>(active_plan.revision + 1U),
               static_cast<int>(replan.new_plan.revision),
               "replan should increment the plan revision");
  assert_equal(1, static_cast<int>(replan.replaced_node_ids.size()),
               "replan should explicitly record the replaced node id");
  assert_true(!replan.new_plan.nodes.empty(),
              "replan should still produce a non-empty recovery plan");
  assert_true(replan.replan_reason.find("observation_failure") == 0U,
              "replan should explain that it reacted to an observation failure");
  assert_true(replan.new_plan.nodes.back().evidence_refs.front() == "belief:evidence:replan" ||
                  replan.new_plan.nodes.back().evidence_refs.back() == "obs-015-replan",
              "replan should keep prior evidence and the failing observation reference");
}

}  // namespace

int main() {
  try {
    test_replan_preserves_plan_identity_and_increments_revision();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}