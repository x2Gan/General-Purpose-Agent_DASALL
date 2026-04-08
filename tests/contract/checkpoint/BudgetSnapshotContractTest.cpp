#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

#include "checkpoint/BudgetSnapshotGuards.h"
#include "support/TestAssertions.h"

namespace {

dasall::contracts::BudgetSnapshot make_valid_budget_snapshot() {
  using dasall::contracts::BudgetSnapshot;
  using dasall::contracts::BudgetType;

  return BudgetSnapshot{
      .snapshot_at_ms = 1000,
      .entries = {
          {
              .budget_type = BudgetType::Token,
              .current = 900,
              .max = 1000,
              .remaining = 100,
              .reject_reason = std::nullopt,
          },
          {
              .budget_type = BudgetType::ToolCall,
              .current = 12,
              .max = 10,
              .remaining = -2,
              .reject_reason = std::string("tool_call_budget_exhausted"),
          },
      },
      .overall_reject_reason = std::string("tool_call_budget_exhausted"),
  };
}

void test_valid_budget_snapshot_passes_guard() {
  using dasall::contracts::validate_budget_snapshot;
  using dasall::tests::support::assert_true;

  // Positive case: one normal entry plus one over-budget entry follows all
  // consistency and reject_reason trigger rules.
  const auto snapshot = make_valid_budget_snapshot();
  const auto result = validate_budget_snapshot(snapshot);

  assert_true(result.ok, "valid budget snapshot should pass validation");
}

void test_remaining_mismatch_is_rejected() {
  using dasall::contracts::validate_budget_snapshot;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  // Negative case: remaining must be derived strictly from max-current.
  auto snapshot = make_valid_budget_snapshot();
  snapshot.entries[0].remaining = 99;

  const auto result = validate_budget_snapshot(snapshot);

  assert_true(!result.ok, "remaining mismatch should be rejected");
  assert_equal("remaining must equal max-current",
               std::string(result.reason),
               "guard should report remaining formula mismatch");
}

void test_reject_reason_without_over_budget_is_rejected() {
  using dasall::contracts::validate_budget_snapshot;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  // Negative case: reject_reason cannot be filled when remaining is non-negative.
  auto snapshot = make_valid_budget_snapshot();
  snapshot.entries[0].reject_reason = std::string("should_not_exist");

  const auto result = validate_budget_snapshot(snapshot);

  assert_true(!result.ok, "reject_reason on non-over-budget entry should be rejected");
  assert_equal("reject_reason must be empty when not over budget",
               std::string(result.reason),
               "guard should enforce reject_reason trigger condition");
}

void test_remaining_computation_overflow_is_rejected() {
  using dasall::contracts::BudgetType;
  using dasall::contracts::validate_budget_snapshot;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  // Negative case: huge unsigned counters that cannot be represented in int64
  // remaining must be rejected explicitly.
  auto snapshot = make_valid_budget_snapshot();
  snapshot.entries[0] = {
      .budget_type = BudgetType::Token,
      .current = static_cast<std::uint64_t>(1) << 63,
      .max = 0,
      .remaining = 0,
      .reject_reason = std::string("overflow"),
  };

  const auto result = validate_budget_snapshot(snapshot);

  assert_true(!result.ok, "overflow remaining computation should be rejected");
  assert_equal("remaining computation overflow",
               std::string(result.reason),
               "guard should fail fast on int64 remaining overflow");
}

}  // namespace

int main() {
  try {
    test_valid_budget_snapshot_passes_guard();
    test_remaining_mismatch_is_rejected();
    test_reject_reason_without_over_budget_is_rejected();
    test_remaining_computation_overflow_is_rejected();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
