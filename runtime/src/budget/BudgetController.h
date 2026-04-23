#pragma once

#include <initializer_list>
#include <mutex>
#include <optional>
#include <string>

#include "budget/IBudgetController.h"

namespace dasall::runtime {

class BudgetController final : public IBudgetController {
 public:
  BudgetController() = default;

  [[nodiscard]] BudgetDecision initialize(const BudgetInitializeRequest& request) override;
    [[nodiscard]] BudgetDecision restore(
            const contracts::BudgetSnapshot& snapshot,
            std::uint64_t started_at_ms);
  [[nodiscard]] BudgetDecision consume(const BudgetConsumeRequest& request) override;
  [[nodiscard]] contracts::BudgetSnapshot snapshot() const override;
  [[nodiscard]] BudgetDecision can_continue() const override;
  [[nodiscard]] BudgetDecision can_replan() const override;
  [[nodiscard]] BudgetDecision can_call_tool() const override;

 private:
  [[nodiscard]] contracts::BudgetSnapshotEntry* find_entry_locked(contracts::BudgetType budget_type);
  [[nodiscard]] const contracts::BudgetSnapshotEntry* find_entry_locked(
      contracts::BudgetType budget_type) const;
  [[nodiscard]] BudgetDecision decision_for_locked(
      std::initializer_list<contracts::BudgetType> required_dimensions,
      std::optional<contracts::BudgetType> allowed_budget_type,
      const std::string& allowed_detail) const;
  void refresh_overall_reject_reason_locked();

  mutable std::mutex budget_mutex_;
  bool initialized_ = false;
  std::uint64_t started_at_ms_ = 0;
  contracts::RuntimeBudget runtime_budget_{};
  contracts::BudgetSnapshot snapshot_{};
};

}  // namespace dasall::runtime