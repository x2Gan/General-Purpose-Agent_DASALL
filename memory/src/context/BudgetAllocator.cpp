#include "context/BudgetAllocator.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <unordered_map>

#include "util/TokenEstimator.h"

namespace dasall::memory {
namespace {

using util::estimate_text_tokens;

struct SlotTemplate {
  const char* slot_name;
  int basis_points;
  int priority;
};

void add_optional_string_tokens(const std::optional<std::string>& value,
                                int& total) {
  if (value.has_value()) {
    total += estimate_text_tokens(*value);
  }
}

void add_optional_string_vector_tokens(
    const std::optional<std::vector<std::string>>& values,
    int& total) {
  if (!values.has_value()) {
    return;
  }

  for (const auto& value : *values) {
    total += estimate_text_tokens(value);
  }
}

std::string normalize_stage(std::string stage) {
  std::transform(stage.begin(), stage.end(), stage.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return stage;
}

std::vector<SlotTemplate> stage_templates(const BudgetPolicy& policy) {
  const auto stage = normalize_stage(policy.stage);

  if (stage == "reflection") {
    return {
        {"user_turn", 6, 100},
        {"current_goal_summary", 12, 95},
        {"policy_digest", 12, 95},
        {"latest_observation_digest_summary", 18, 100},
        {"recent_history", 10, 50},
        {"summary_memory", 18, 85},
        {"belief_state_summary", 14, 80},
        {"retrieval_evidence", 3, 40},
        {"active_tools", 2, 30},
    };
  }

  if (stage == "reasoning") {
    return {
        {"user_turn", 8, 100},
        {"current_goal_summary", 15, 95},
        {"policy_digest", 10, 90},
        {"latest_observation_digest_summary", 17, 100},
        {"recent_history", 18, 55},
        {"summary_memory", 10, 70},
        {"belief_state_summary", 10, 75},
        {"retrieval_evidence", 5, 45},
        {"active_tools", 2, 35},
    };
  }

  return {
      {"user_turn", 8, 100},
      {"current_goal_summary", 18, 95},
      {"policy_digest", 12, 95},
      {"latest_observation_digest_summary", 7, 85},
      {"recent_history", 18, 50},
      {"summary_memory", 12, 70},
      {"belief_state_summary", 12, 75},
      {"retrieval_evidence", 6, 40},
      {"active_tools", 2, 30},
  };
}

SlotBudget* find_slot_budget(std::vector<SlotBudget>& slot_budgets,
                             const std::string& slot_name) {
  const auto it = std::find_if(slot_budgets.begin(), slot_budgets.end(),
                               [&slot_name](const SlotBudget& candidate) {
                                 return candidate.slot_name == slot_name;
                               });
  if (it == slot_budgets.end()) {
    return nullptr;
  }

  return &(*it);
}

void transfer_budget(std::vector<SlotBudget>& slot_budgets,
                     const std::string& from,
                     const std::string& to,
                     int tokens) {
  if (tokens <= 0) {
    return;
  }

  auto* from_slot = find_slot_budget(slot_budgets, from);
  auto* to_slot = find_slot_budget(slot_budgets, to);
  if (from_slot == nullptr || to_slot == nullptr) {
    return;
  }

  const auto transferred_tokens = std::min(tokens, from_slot->allocated_tokens);
  from_slot->allocated_tokens -= transferred_tokens;
  to_slot->allocated_tokens += transferred_tokens;
}

int estimate_user_turn_tokens(const CandidateSet& candidates) {
  if (candidates.session_bundle.recent_turns.empty()) {
    return 0;
  }

  return estimate_text_tokens(
      candidates.session_bundle.recent_turns.front().user_input.value_or(std::string{}));
}

int estimate_recent_history_tokens(const CandidateSet& candidates) {
  int total = 0;
  for (std::size_t index = 0; index < candidates.session_bundle.recent_turns.size(); ++index) {
    const auto& turn = candidates.session_bundle.recent_turns[index];
    if (index != 0U) {
      add_optional_string_tokens(turn.user_input, total);
    }
    add_optional_string_tokens(turn.agent_response, total);
    add_optional_string_vector_tokens(turn.tool_call_refs, total);
    add_optional_string_vector_tokens(turn.observation_refs, total);
  }
  return total;
}

int estimate_summary_tokens(const CandidateSet& candidates) {
  if (!candidates.latest_summary.has_value()) {
    return 0;
  }

  int total = 0;
  add_optional_string_tokens(candidates.latest_summary->summary_text, total);
  add_optional_string_vector_tokens(candidates.latest_summary->decisions_made, total);
  add_optional_string_vector_tokens(candidates.latest_summary->confirmed_facts, total);
  add_optional_string_vector_tokens(candidates.latest_summary->tool_outcomes, total);
  return total;
}

int estimate_belief_state_tokens(const CandidateSet& candidates) {
  int total = 0;
  for (const auto& fact : candidates.relevant_facts) {
    add_optional_string_tokens(fact.fact_text, total);
    add_optional_string_tokens(fact.fact_type, total);
  }
  return total;
}

int estimate_retrieval_evidence_tokens(const CandidateSet& candidates) {
  int total = 0;
  for (const auto& evidence : candidates.external_evidence) {
    total += estimate_text_tokens(evidence);
  }
  for (const auto& hit : candidates.vector_hits) {
    total += estimate_text_tokens(hit.text_snippet);
    total += estimate_text_tokens(hit.doc_type);
  }
  return total;
}

int estimate_slot_tokens(const CandidateSet& candidates, std::string_view slot_name) {
  if (slot_name == "user_turn") {
    return estimate_user_turn_tokens(candidates);
  }
  if (slot_name == "recent_history") {
    return estimate_recent_history_tokens(candidates);
  }
  if (slot_name == "summary_memory") {
    return estimate_summary_tokens(candidates);
  }
  if (slot_name == "belief_state_summary") {
    return estimate_belief_state_tokens(candidates);
  }
  if (slot_name == "retrieval_evidence") {
    return estimate_retrieval_evidence_tokens(candidates);
  }
  return 0;
}

}  // namespace

BudgetAllocator::BudgetAllocator(const MemoryConfig& config)
    : context_config_(config.context) {}

BudgetPlan BudgetAllocator::allocate(const CandidateSet& candidates,
                                     const BudgetPolicy& policy) const {
  BudgetPlan plan;
  plan.total_token_budget = std::max(0, policy.total_token_budget);
  plan.slot_budgets = compute_slot_budgets(candidates, policy);

  for (const auto& slot_budget : plan.slot_budgets) {
    plan.estimated_total_tokens += slot_budget.estimated_tokens;
  }

  plan.trim_actions = compute_trim_actions(
      plan.slot_budgets, plan.total_token_budget, plan.estimated_total_tokens);

  int reduced_estimated_tokens = plan.estimated_total_tokens;
  for (const auto& trim_action : plan.trim_actions) {
    const auto budget_it = std::find_if(
        plan.slot_budgets.begin(), plan.slot_budgets.end(),
        [&trim_action](const SlotBudget& slot_budget) {
          return slot_budget.slot_name == trim_action.slot_name;
        });
    if (budget_it != plan.slot_budgets.end()) {
      reduced_estimated_tokens -=
          std::max(0, budget_it->estimated_tokens - trim_action.target_tokens);
    }
  }

  plan.over_budget = reduced_estimated_tokens > plan.total_token_budget;
  return plan;
}

std::vector<SlotBudget> BudgetAllocator::compute_slot_budgets(
    const CandidateSet& candidates,
    const BudgetPolicy& policy) const {
  std::vector<SlotBudget> slot_budgets;
  const auto templates = stage_templates(policy);
  const auto total_budget = std::max(0, policy.total_token_budget);

  for (const auto& slot_template : templates) {
    slot_budgets.push_back(SlotBudget{
        .slot_name = slot_template.slot_name,
        .allocated_tokens = (total_budget * slot_template.basis_points) / 100,
        .estimated_tokens = estimate_slot_tokens(candidates, slot_template.slot_name),
        .priority = slot_template.priority,
    });
  }

  if (policy.risk_level >= 2) {
    const auto risk_shift = std::max(1, total_budget / 20);
    transfer_budget(slot_budgets, "recent_history", "policy_digest", risk_shift);
    transfer_budget(slot_budgets, "summary_memory", "latest_observation_digest_summary",
                    std::max(1, total_budget / 33));
  }

  if (policy.latency_budget_ms > 0 && policy.latency_budget_ms <= 200) {
    transfer_budget(slot_budgets, "retrieval_evidence", "current_goal_summary",
                    std::max(1, total_budget / 33));
    transfer_budget(slot_budgets, "recent_history",
                    "latest_observation_digest_summary",
                    std::max(1, total_budget / 50));
  }

  (void)context_config_;
  return slot_budgets;
}

std::vector<TrimAction> BudgetAllocator::compute_trim_actions(
    const std::vector<SlotBudget>& slot_budgets,
    int total_token_budget,
    int total_estimated_tokens) const {
  std::vector<TrimAction> trim_actions;
  int excess_tokens = std::max(0, total_estimated_tokens - total_token_budget);
  if (excess_tokens == 0) {
    return trim_actions;
  }

  auto ordered_slot_budgets = slot_budgets;
  std::sort(ordered_slot_budgets.begin(), ordered_slot_budgets.end(),
            [](const SlotBudget& left, const SlotBudget& right) {
              if (left.priority != right.priority) {
                return left.priority < right.priority;
              }
              if (left.estimated_tokens != right.estimated_tokens) {
                return left.estimated_tokens > right.estimated_tokens;
              }
              return left.slot_name < right.slot_name;
            });

  for (const auto& slot_budget : ordered_slot_budgets) {
    if (excess_tokens <= 0) {
      break;
    }

    const auto min_target_tokens = std::min(slot_budget.allocated_tokens,
                                            slot_budget.estimated_tokens);
    const auto reducible_tokens = slot_budget.estimated_tokens - min_target_tokens;
    if (reducible_tokens <= 0) {
      continue;
    }

    const auto trimmed_tokens = std::min(excess_tokens, reducible_tokens);
    trim_actions.push_back(TrimAction{
        .slot_name = slot_budget.slot_name,
        .target_tokens = slot_budget.estimated_tokens - trimmed_tokens,
    });
    excess_tokens -= trimmed_tokens;
  }

  return trim_actions;
}

}  // namespace dasall::memory