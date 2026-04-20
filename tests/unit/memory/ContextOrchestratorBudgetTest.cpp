#include <algorithm>
#include <exception>
#include <iostream>
#include <string>

#include "config/MemoryConfig.h"
#include "context/BudgetAllocator.h"
#include "support/TestAssertions.h"

namespace {

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

dasall::memory::CandidateSet make_budget_candidate_set() {
  dasall::memory::CandidateSet set;

  dasall::contracts::Turn latest_turn;
  latest_turn.user_input = std::string(200, 'u');
  latest_turn.agent_response = std::string(300, 'a');
  set.session_bundle.recent_turns = {latest_turn};

  dasall::contracts::SummaryMemory summary;
  summary.summary_text = std::string(180, 's');
  set.latest_summary = summary;

  dasall::contracts::MemoryFact fact;
  fact.fact_text = std::string(140, 'f');
  fact.fact_type = "constraint";
  set.relevant_facts = {fact};

  set.external_evidence = {std::string(150, 'e')};
  set.vector_hits = {dasall::memory::VectorHit{
      .doc_id = "doc-ctx-budget",
      .doc_type = "evidence",
      .score = 0.9F,
      .text_snippet = std::string(160, 'v'),
  }};

  return set;
}

void test_context_budget_plan_shifts_toward_policy_and_latest_observation_under_risk_and_latency() {
  using dasall::tests::support::assert_true;

  dasall::memory::MemoryConfig config;
  dasall::memory::BudgetAllocator allocator(config);
  const auto candidates = make_budget_candidate_set();

  const auto baseline = allocator.allocate(
      candidates,
      dasall::memory::BudgetPolicy{
          .total_token_budget = 200,
          .stage = "reasoning",
      });

  const auto guarded = allocator.allocate(
      candidates,
      dasall::memory::BudgetPolicy{
          .total_token_budget = 200,
          .stage = "reasoning",
          .risk_level = 3,
          .latency_budget_ms = 100,
      });

  assert_true(find_slot_budget(guarded, "policy_digest").allocated_tokens >
                  find_slot_budget(baseline, "policy_digest").allocated_tokens,
              "high-risk reasoning budgets should increase the policy digest allocation");
  assert_true(find_slot_budget(guarded, "latest_observation_digest_summary").allocated_tokens >
                  find_slot_budget(baseline, "latest_observation_digest_summary").allocated_tokens,
              "high-risk reasoning budgets should increase the latest observation allocation");
  assert_true(find_slot_budget(guarded, "retrieval_evidence").allocated_tokens <
                  find_slot_budget(baseline, "retrieval_evidence").allocated_tokens,
              "latency-constrained reasoning budgets should shrink retrieval evidence first");
  assert_true(find_slot_budget(guarded, "recent_history").allocated_tokens <
                  find_slot_budget(baseline, "recent_history").allocated_tokens,
              "risk and latency shifts should take budget away from recent history before high-priority slots");
}

}  // namespace

int main() {
  try {
    test_context_budget_plan_shifts_toward_policy_and_latest_observation_under_risk_and_latency();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}