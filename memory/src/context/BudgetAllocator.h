#pragma once

#include <string>
#include <vector>

#include "config/MemoryConfig.h"
#include "context/CandidateCollector.h"

namespace dasall::memory {

struct BudgetPolicy {
  int total_token_budget = 0;
  std::string stage;
  int risk_level = 0;
  int latency_budget_ms = 0;
};

struct SlotBudget {
  std::string slot_name;
  int allocated_tokens = 0;
  int estimated_tokens = 0;
  int priority = 0;
};

struct TrimAction {
  std::string slot_name;
  int target_tokens = 0;
};

struct BudgetPlan {
  std::vector<SlotBudget> slot_budgets;
  std::vector<TrimAction> trim_actions;
  int total_token_budget = 0;
  int estimated_total_tokens = 0;
  bool over_budget = false;
};

class BudgetAllocator {
 public:
  explicit BudgetAllocator(const MemoryConfig& config);

  [[nodiscard]] BudgetPlan allocate(const CandidateSet& candidates,
                                    const BudgetPolicy& policy) const;

 private:
  [[nodiscard]] std::vector<SlotBudget> compute_slot_budgets(
      const CandidateSet& candidates,
      const BudgetPolicy& policy) const;
  [[nodiscard]] std::vector<TrimAction> compute_trim_actions(
      const std::vector<SlotBudget>& slot_budgets,
      int total_token_budget,
      int total_estimated_tokens) const;

  ContextConfig context_config_{};
};

}  // namespace dasall::memory