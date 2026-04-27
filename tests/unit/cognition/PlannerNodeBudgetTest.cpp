#include <exception>
#include <iostream>
#include <string>

#include "planning/Planner.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::BudgetContext;
using dasall::cognition::CognitionConfig;
using dasall::cognition::PlanningRequest;
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

[[nodiscard]] PlanningRequest make_planning_request(float budget_utilization,
                                                    bool near_budget_limit) {
  PlanningRequest request;
  request.caller_domain = "runtime.agent_orchestrator";
  request.request_id = near_budget_limit ? "req-015-budget-high" : "req-015-budget-mid";
  request.trace_id = "trace-015-budget";
  request.profile_id = "desktop_full";

  request.goal_contract.goal_id = std::string("goal-015-budget");
  request.goal_contract.request_id = request.request_id;
  request.goal_contract.goal_description =
      std::string("collect verifiable evidence for quarterly sales in Berlin");
  request.goal_contract.success_criteria =
      std::string("return evidence-backed quarterly sales findings for Berlin");
  request.goal_contract.status = dasall::contracts::GoalStatus::Active;
  request.goal_contract.created_at = 1712345600000;

  request.context_packet.request_id = request.request_id;
  request.context_packet.user_turn =
      std::string("Find quarterly sales evidence for Berlin and summarize it");
  request.context_packet.current_goal_summary =
      std::string("find quarterly sales evidence for Berlin");

  request.belief_state.request_id = request.request_id;
  request.belief_state.confirmed_facts =
      std::vector<std::string>{std::string("Berlin is the requested city")};
  request.belief_state.hypotheses =
      std::vector<std::string>{std::string("dataset contains quarterly sales"),
                               std::string("evidence can be summarized directly")};
  request.belief_state.assumptions =
      std::vector<std::string>{std::string("budget is sufficient for one more lookup")};
  request.belief_state.evidence_refs =
      std::vector<std::string>{std::string("belief:evidence:budget")};
  request.belief_state.confidence = 0.80F;

  request.perception_result.intent_summary =
      std::string("collect evidence for quarterly sales in Berlin");
  request.perception_result.task_type = std::string("action_decision");
  request.perception_result.entities = {
      EntityCandidate{.name = "tool",
                      .value = "agent.dataset",
                      .confidence = 0.94F,
                      .evidence_refs = {std::string("tool:agent.dataset")}},
  };
  request.perception_result.confidence = 0.83F;

  request.budget_context = BudgetContext{.total_budget_tokens = 1000U,
                                         .consumed_tokens = static_cast<std::uint32_t>(budget_utilization * 1000.0F),
                                         .remaining_tokens = static_cast<std::uint32_t>((1.0F - budget_utilization) * 1000.0F),
                                         .budget_utilization = budget_utilization,
                                         .context_was_truncated = false,
                                         .near_budget_limit = near_budget_limit};
  return request;
}

void test_mid_budget_pressure_compresses_plan_to_three_nodes() {
  Planner planner(make_config(6U, 4U));

  const auto graph = planner.build_plan(make_planning_request(0.65F, false));

  assert_equal(3, static_cast<int>(graph.nodes.size()),
               "mid-budget pressure should compress the plan to three nodes");
  assert_true(graph.plan_rationale.find("compressed_for_budget") != std::string::npos,
              "mid-budget compression should be visible in the plan rationale");
}

void test_high_budget_pressure_clamps_plan_to_shallow_graph() {
  Planner planner(make_config(6U, 4U));

  const auto graph = planner.build_plan(make_planning_request(0.85F, true));

  assert_equal(2, static_cast<int>(graph.nodes.size()),
               "high budget pressure should clamp the plan to two shallow nodes");
  assert_true(graph.estimated_complexity <= 2U,
              "high budget pressure should also clamp the exposed plan complexity");
}

}  // namespace

int main() {
  try {
    test_mid_budget_pressure_compresses_plan_to_three_nodes();
    test_high_budget_pressure_clamps_plan_to_shallow_graph();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}