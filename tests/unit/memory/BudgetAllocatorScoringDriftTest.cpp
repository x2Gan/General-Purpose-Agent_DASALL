#include <algorithm>
#include <exception>
#include <iostream>
#include <string>

#include "config/MemoryConfig.h"
#include "context/BudgetAllocator.h"
#include "support/TestAssertions.h"

namespace {

dasall::memory::CandidateSet make_candidate_set(bool reverse_facts) {
  dasall::memory::CandidateSet set;

  dasall::contracts::Turn latest_turn;
  latest_turn.turn_id = "turn-scoring-drift-002";
  latest_turn.user_input = std::string(220, 'u');
  latest_turn.agent_response = std::string(260, 'a');
  latest_turn.tool_call_refs = std::vector<std::string>{std::string(90, 't')};

  dasall::contracts::Turn older_turn;
  older_turn.turn_id = "turn-scoring-drift-001";
  older_turn.user_input = std::string(180, 'p');
  older_turn.agent_response = std::string(200, 'r');

  set.session_bundle.recent_turns = {latest_turn, older_turn};

  dasall::contracts::SummaryMemory summary;
  summary.summary_text = std::string(180, 's');
  summary.confirmed_facts = std::vector<std::string>{std::string(70, 'f')};
  summary.decisions_made = std::vector<std::string>{std::string(60, 'd')};
  set.latest_summary = summary;

  dasall::contracts::MemoryFact first_fact;
  first_fact.fact_text = std::string(140, 'x');
  first_fact.fact_type = "constraint";

  dasall::contracts::MemoryFact second_fact;
  second_fact.fact_text = std::string(160, 'y');
  second_fact.fact_type = "preference";

  set.relevant_facts = {first_fact, second_fact};
  if (reverse_facts) {
    std::reverse(set.relevant_facts.begin(), set.relevant_facts.end());
  }

  set.external_evidence = {std::string(120, 'e')};
  set.vector_hits = {dasall::memory::VectorHit{
      .doc_id = "doc-scoring-drift",
      .doc_type = "summary",
      .score = 0.84F,
      .text_snippet = std::string(110, 'v'),
  }};

  return set;
}

void assert_same_plan(const dasall::memory::BudgetPlan& left,
                      const dasall::memory::BudgetPlan& right,
                      const std::string& label) {
  using dasall::tests::support::assert_equal;

  assert_equal(left.total_token_budget, right.total_token_budget,
               label + ": total budget should remain stable");
  assert_equal(left.estimated_total_tokens, right.estimated_total_tokens,
               label + ": estimated token load should not drift when candidate order changes");
  assert_equal(left.over_budget, right.over_budget,
               label + ": over-budget classification should not drift when candidate order changes");
  assert_equal(static_cast<int>(left.slot_budgets.size()),
               static_cast<int>(right.slot_budgets.size()),
               label + ": slot budget count should remain stable");
  for (std::size_t index = 0; index < left.slot_budgets.size(); ++index) {
    assert_equal(left.slot_budgets[index].slot_name,
                 right.slot_budgets[index].slot_name,
                 label + ": slot ordering should remain stable");
    assert_equal(left.slot_budgets[index].allocated_tokens,
                 right.slot_budgets[index].allocated_tokens,
                 label + ": allocated tokens should remain stable");
    assert_equal(left.slot_budgets[index].estimated_tokens,
                 right.slot_budgets[index].estimated_tokens,
                 label + ": estimated slot tokens should remain stable");
    assert_equal(left.slot_budgets[index].priority,
                 right.slot_budgets[index].priority,
                 label + ": slot priority should remain stable");
  }
  assert_equal(static_cast<int>(left.trim_actions.size()),
               static_cast<int>(right.trim_actions.size()),
               label + ": trim action count should remain stable");
  for (std::size_t index = 0; index < left.trim_actions.size(); ++index) {
    assert_equal(left.trim_actions[index].slot_name,
                 right.trim_actions[index].slot_name,
                 label + ": trim targets should remain stable");
    assert_equal(left.trim_actions[index].target_tokens,
                 right.trim_actions[index].target_tokens,
                 label + ": trim token counts should remain stable");
  }
}

void test_budget_allocator_stays_stable_when_candidate_order_changes(
    dasall::memory::TokenEstimatorBackend backend) {
  dasall::memory::MemoryConfig config;
  config.token_estimator = backend;
  config.context.scoring.composite_enabled = true;
  config.context.scoring.confidence_weight = 0.20;
  config.context.scoring.recency_weight = 0.35;
  config.context.scoring.hit_rate_weight = 0.25;
  config.context.scoring.source_weight = 0.20;

  dasall::memory::BudgetAllocator allocator(config);
  const auto policy = dasall::memory::BudgetPolicy{
      .total_token_budget = 120,
      .stage = "planning",
      .risk_level = 1,
      .latency_budget_ms = 150,
  };

  const auto forward_plan = allocator.allocate(make_candidate_set(false), policy);
  const auto reversed_plan = allocator.allocate(make_candidate_set(true), policy);

  const auto backend_label =
      std::string(dasall::memory::to_string_view(backend));
  assert_same_plan(forward_plan, reversed_plan, backend_label);
}

}  // namespace

int main() {
  try {
    test_budget_allocator_stays_stable_when_candidate_order_changes(
        dasall::memory::TokenEstimatorBackend::Tiktoken);
    test_budget_allocator_stays_stable_when_candidate_order_changes(
        dasall::memory::TokenEstimatorBackend::Heuristic);
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}