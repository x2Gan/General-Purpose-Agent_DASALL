#pragma once

#include <string_view>

#include "checkpoint/RuntimeBudget.h"

namespace dasall::contracts {

struct RuntimeBudgetGuardResult {
  bool ok = false;
  std::string_view reason = "runtime budget validation failed";
};

// Validates WP02-T007 required fields and unit-safe threshold semantics.
// Unit semantics are encoded by field naming and reinforced here by requiring
// each budget dimension to be a positive quantity.
inline RuntimeBudgetGuardResult validate_runtime_budget(const RuntimeBudget& budget) {
  if (!budget.max_tokens.has_value()) {
    return RuntimeBudgetGuardResult{.ok = false, .reason = "max_tokens is required"};
  }
  if (*budget.max_tokens == 0U) {
    return RuntimeBudgetGuardResult{.ok = false, .reason = "max_tokens must be greater than zero"};
  }

  if (!budget.max_turns.has_value()) {
    return RuntimeBudgetGuardResult{.ok = false, .reason = "max_turns is required"};
  }
  if (*budget.max_turns == 0U) {
    return RuntimeBudgetGuardResult{.ok = false, .reason = "max_turns must be greater than zero"};
  }

  if (!budget.max_tool_calls.has_value()) {
    return RuntimeBudgetGuardResult{.ok = false, .reason = "max_tool_calls is required"};
  }
  if (*budget.max_tool_calls == 0U) {
    return RuntimeBudgetGuardResult{.ok = false, .reason = "max_tool_calls must be greater than zero"};
  }

  if (!budget.max_latency_ms.has_value()) {
    return RuntimeBudgetGuardResult{.ok = false, .reason = "max_latency_ms is required"};
  }
  if (*budget.max_latency_ms == 0U) {
    return RuntimeBudgetGuardResult{.ok = false, .reason = "max_latency_ms must be greater than zero"};
  }

  if (!budget.max_replan_count.has_value()) {
    return RuntimeBudgetGuardResult{.ok = false, .reason = "max_replan_count is required"};
  }
  if (*budget.max_replan_count == 0U) {
    return RuntimeBudgetGuardResult{.ok = false, .reason = "max_replan_count must be greater than zero"};
  }

  return RuntimeBudgetGuardResult{.ok = true, .reason = "runtime budget is valid"};
}

}  // namespace dasall::contracts
