#include <exception>
#include <iostream>
#include <string>

#include "checkpoint/RuntimeBudgetGuards.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::contracts::RuntimeBudget make_valid_runtime_budget() {
  using dasall::contracts::RuntimeBudget;

  return RuntimeBudget{
      .max_tokens = 4096,
      .max_turns = 16,
      .max_tool_calls = 8,
      .max_latency_ms = 120000,
      .max_replan_count = 3,
  };
}

void test_valid_runtime_budget_passes_guard() {
  using dasall::contracts::validate_runtime_budget;
  using dasall::tests::support::assert_true;

  // Positive case: all five budget dimensions are present and positive.
  const auto budget = make_valid_runtime_budget();
  const auto result = validate_runtime_budget(budget);

  assert_true(result.ok, "valid runtime budget should pass guard validation");
}

void test_missing_required_budget_field_is_rejected() {
  using dasall::contracts::validate_runtime_budget;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  // Negative case: required field max_turns is missing.
  auto budget = make_valid_runtime_budget();
  budget.max_turns.reset();

  const auto result = validate_runtime_budget(budget);

  assert_true(!result.ok, "missing max_turns should be rejected");
  assert_equal("max_turns is required",
               std::string(result.reason),
               "guard should report the missing required field");
}

void test_zero_latency_budget_is_rejected() {
  using dasall::contracts::validate_runtime_budget;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  // Negative case: latency budget uses ms unit and must be positive.
  auto budget = make_valid_runtime_budget();
  budget.max_latency_ms = 0;

  const auto result = validate_runtime_budget(budget);

  assert_true(!result.ok, "zero max_latency_ms should be rejected");
  assert_equal("max_latency_ms must be greater than zero",
               std::string(result.reason),
               "guard should enforce positive latency threshold");
}

}  // namespace

int main() {
  try {
    test_valid_runtime_budget_passes_guard();
    test_missing_required_budget_field_is_rejected();
    test_zero_latency_budget_is_rejected();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
