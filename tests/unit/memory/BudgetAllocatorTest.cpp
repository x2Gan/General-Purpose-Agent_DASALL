#include <algorithm>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "config/MemoryConfig.h"
#include "context/BudgetAllocator.h"
#include "support/TestAssertions.h"

namespace {

std::string backend_name(dasall::memory::TokenEstimatorBackend backend) {
  return std::string(dasall::memory::to_string_view(backend));
}

const dasall::memory::SlotBudget& find_slot_budget(
    const dasall::memory::BudgetPlan& plan,
    const std::string& slot_name) {
  const auto it = std::find_if(plan.slot_budgets.begin(), plan.slot_budgets.end(),
                               [&slot_name](const dasall::memory::SlotBudget& slot) {
                                 return slot.slot_name == slot_name;
                               });
  if (it == plan.slot_budgets.end()) {
    throw std::runtime_error("missing slot budget: " + slot_name);
  }
  return *it;
}

bool contains_trim_action(const dasall::memory::BudgetPlan& plan,
                          const std::string& slot_name) {
  return std::any_of(plan.trim_actions.begin(), plan.trim_actions.end(),
                     [&slot_name](const dasall::memory::TrimAction& action) {
                       return action.slot_name == slot_name;
                     });
}

dasall::memory::CandidateSet make_pressure_candidate_set() {
  dasall::memory::CandidateSet set;

  dasall::contracts::Turn latest_turn;
  latest_turn.turn_id = "turn-017-002";
  latest_turn.user_input = std::string(240, 'u');
  latest_turn.agent_response = std::string(320, 'a');
  latest_turn.tool_call_refs = std::vector<std::string>{std::string(120, 't')};
  latest_turn.observation_refs = std::vector<std::string>{std::string(120, 'o')};

  dasall::contracts::Turn older_turn;
  older_turn.turn_id = "turn-017-001";
  older_turn.user_input = std::string(280, 'p');
  older_turn.agent_response = std::string(320, 'r');

  set.session_bundle.recent_turns = {latest_turn, older_turn};

  dasall::contracts::SummaryMemory summary;
  summary.summary_text = std::string(200, 's');
  summary.decisions_made = std::vector<std::string>{std::string(80, 'd')};
  summary.confirmed_facts = std::vector<std::string>{std::string(80, 'f')};
  summary.tool_outcomes = std::vector<std::string>{std::string(80, 'g')};
  set.latest_summary = summary;

  dasall::contracts::MemoryFact fact;
  fact.fact_text = std::string(180, 'b');
  fact.fact_type = "constraint";
  set.relevant_facts = {fact};

  set.external_evidence = {std::string(160, 'e')};
  set.vector_hits = {dasall::memory::VectorHit{
      .doc_id = "doc-017",
      .doc_type = "summary",
      .score = 0.91F,
      .text_snippet = std::string(180, 'v'),
  }};

  return set;
}

void test_budget_allocator_prioritizes_goal_and_policy_in_planning_stage(
    dasall::memory::TokenEstimatorBackend backend) {
  using dasall::tests::support::assert_true;

  dasall::memory::MemoryConfig config;
  config.token_estimator = backend;
  dasall::memory::BudgetAllocator allocator(config);

  const auto plan = allocator.allocate(
      make_pressure_candidate_set(),
      dasall::memory::BudgetPolicy{
          .total_token_budget = 120,
          .stage = "planning",
      });

  const auto current_goal = find_slot_budget(plan, "current_goal_summary");
  const auto policy = find_slot_budget(plan, "policy_digest");
  const auto active_tools = find_slot_budget(plan, "active_tools");
  const auto retrieval = find_slot_budget(plan, "retrieval_evidence");
  const auto backend_label = backend_name(backend);

  assert_true(current_goal.allocated_tokens > active_tools.allocated_tokens,
              backend_label + ": planning stage should reserve more budget for current goal than active tools");
  assert_true(policy.allocated_tokens > retrieval.allocated_tokens,
              backend_label + ": planning stage should reserve more budget for policy digest than retrieval evidence");
  assert_true(!plan.trim_actions.empty(),
              backend_label + ": planning stage should emit trim actions when candidate tokens exceed the total budget");
  assert_true(contains_trim_action(plan, "recent_history"),
              backend_label + ": planning stage trim actions should target recent history under budget pressure");
  assert_true(!plan.over_budget,
              backend_label + ": planning stage should produce a non-over-budget plan when trim actions can absorb the excess");
}

void test_budget_allocator_uses_estimated_slot_usage_in_trim_targets(
    dasall::memory::TokenEstimatorBackend backend) {
  using dasall::tests::support::assert_true;

  dasall::memory::MemoryConfig config;
  config.token_estimator = backend;
  dasall::memory::BudgetAllocator allocator(config);

  const auto plan = allocator.allocate(
      make_pressure_candidate_set(),
      dasall::memory::BudgetPolicy{
          .total_token_budget = 90,
          .stage = "planning",
      });

  const auto recent_history = find_slot_budget(plan, "recent_history");
  const auto trim_it = std::find_if(
      plan.trim_actions.begin(), plan.trim_actions.end(),
      [](const dasall::memory::TrimAction& action) {
        return action.slot_name == "recent_history";
      });
  const auto backend_label = backend_name(backend);

  assert_true(trim_it != plan.trim_actions.end(),
              backend_label + ": recent history should receive a trim target when it dominates estimated usage");
  assert_true(trim_it->target_tokens <= recent_history.estimated_tokens,
              backend_label + ": trim target should never exceed the estimated slot usage");
  assert_true(trim_it->target_tokens >= recent_history.allocated_tokens,
              backend_label + ": trim target should not cut below the slot's allocated budget floor");
}

}  // namespace

int main() {
  try {
    test_budget_allocator_prioritizes_goal_and_policy_in_planning_stage(
        dasall::memory::TokenEstimatorBackend::Tiktoken);
    test_budget_allocator_uses_estimated_slot_usage_in_trim_targets(
        dasall::memory::TokenEstimatorBackend::Tiktoken);
    test_budget_allocator_prioritizes_goal_and_policy_in_planning_stage(
        dasall::memory::TokenEstimatorBackend::Heuristic);
    test_budget_allocator_uses_estimated_slot_usage_in_trim_targets(
        dasall::memory::TokenEstimatorBackend::Heuristic);
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}