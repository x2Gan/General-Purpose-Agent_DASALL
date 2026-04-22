#pragma once

#include "budget/BudgetDecision.h"

namespace dasall::runtime {

class IBudgetController {
 public:
  virtual ~IBudgetController() = default;

  [[nodiscard]] virtual BudgetDecision initialize(const BudgetInitializeRequest& request) = 0;
  [[nodiscard]] virtual BudgetDecision consume(const BudgetConsumeRequest& request) = 0;
  [[nodiscard]] virtual contracts::BudgetSnapshot snapshot() const = 0;
  [[nodiscard]] virtual BudgetDecision can_continue() const = 0;
  [[nodiscard]] virtual BudgetDecision can_replan() const = 0;
  [[nodiscard]] virtual BudgetDecision can_call_tool() const = 0;
};

}  // namespace dasall::runtime