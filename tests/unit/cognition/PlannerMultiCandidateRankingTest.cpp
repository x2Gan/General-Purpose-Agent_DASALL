#include <algorithm>
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
using dasall::cognition::planning::PlanCandidateKind;
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
  request.request_id = near_budget_limit ? "req-013-budget-tight"
                                         : "req-013-budget-normal";
  request.trace_id = "trace-013";
  request.profile_id = "desktop_full";

  request.goal_contract.goal_id = "goal-013";
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
      std::vector<std::string>{std::string("the builtin dataset contains quarterly sales")};
  request.belief_state.assumptions =
      std::vector<std::string>{std::string("one more lookup fits inside the available budget")};
  request.belief_state.evidence_refs =
      std::vector<std::string>{std::string("belief:evidence:berlin")};
  request.belief_state.confidence = 0.86F;

  request.perception_result.intent_summary =
      std::string("collect evidence for quarterly sales in Berlin");
  request.perception_result.task_type = std::string("action_decision");
  request.perception_result.entities = {
      EntityCandidate{.name = "tool",
                      .value = "agent.dataset",
                      .confidence = 0.95F,
                      .evidence_refs = {std::string("tool:agent.dataset")}},
  };
  request.perception_result.confidence = 0.88F;

  request.budget_context = BudgetContext{.total_budget_tokens = 1000U,
                                         .consumed_tokens = static_cast<std::uint32_t>(budget_utilization * 1000.0F),
                                         .remaining_tokens = static_cast<std::uint32_t>((1.0F - budget_utilization) * 1000.0F),
                                         .budget_utilization = budget_utilization,
                                         .context_was_truncated = false,
                                         .near_budget_limit = near_budget_limit};
  return request;
}

void test_normal_budget_prefers_canonical_candidate_and_keeps_two_backups() {
  Planner planner(make_config(6U, 6U));

  const auto ranked = planner.build_ranked_plan_candidates(
      make_planning_request(0.30F, false));

  assert_true(ranked.primary_candidate.has_value(),
              "normal-budget planning should produce a primary candidate");
  assert_equal(3, static_cast<int>(ranked.ranked_candidates.size()),
               "normal-budget planning should keep three ranked candidates");
  assert_equal(2, static_cast<int>(ranked.backup_candidates.size()),
               "normal-budget planning should keep two backup candidates");
  assert_true(ranked.primary_candidate->candidate_kind == PlanCandidateKind::Canonical,
              "canonical staged plan should stay primary under normal budget");
  assert_true(ranked.backup_candidates.front().candidate_kind ==
                  PlanCandidateKind::LeanExecution,
              "lean execution plan should be the first backup under normal budget");
  assert_true(ranked.ranked_candidates[0].ranking_score >=
                  ranked.ranked_candidates[1].ranking_score &&
                  ranked.ranked_candidates[1].ranking_score >=
                  ranked.ranked_candidates[2].ranking_score,
              "ranked candidates should be sorted by descending ranking score");

  const auto has_direct_response_backup = std::any_of(
      ranked.backup_candidates.begin(),
      ranked.backup_candidates.end(),
      [](const auto& candidate) {
        return candidate.candidate_kind == PlanCandidateKind::DirectResponseFallback;
      });
  assert_true(has_direct_response_backup,
              "normal-budget planning should retain a direct-response backup candidate");
}

void test_high_budget_pressure_reduces_candidates_and_preserves_budget_fit_order() {
  Planner planner(make_config(6U, 6U));

  const auto ranked = planner.build_ranked_plan_candidates(
      make_planning_request(0.85F, true));

  assert_true(ranked.primary_candidate.has_value(),
              "tight-budget planning should still produce a primary candidate");
  assert_equal(2, static_cast<int>(ranked.ranked_candidates.size()),
               "tight-budget planning should shrink output to two ranked candidates");
  assert_equal(1, static_cast<int>(ranked.backup_candidates.size()),
               "tight-budget planning should retain one backup candidate");
  assert_true(ranked.primary_candidate->candidate_kind == PlanCandidateKind::Canonical,
              "the shallow canonical plan should remain primary under tight budget");
  assert_true(ranked.primary_candidate->plan_graph.nodes.size() <= 2U,
              "tight-budget primary candidate should stay within the shallow node cap");
  assert_true(ranked.backup_candidates.front().candidate_kind ==
                  PlanCandidateKind::DirectResponseFallback,
              "tight-budget backup should collapse to the direct-response fallback");
  assert_true(ranked.primary_candidate->ranking_score >=
                  ranked.backup_candidates.front().ranking_score,
              "primary candidate should outrank the remaining backup under tight budget");
}

}  // namespace

int main() {
  try {
    test_normal_budget_prefers_canonical_candidate_and_keeps_two_backups();
    test_high_budget_pressure_reduces_candidates_and_preserves_budget_fit_order();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}