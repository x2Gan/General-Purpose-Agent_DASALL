#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

#include "checkpoint/BudgetSnapshot.h"

namespace dasall::contracts {

struct BudgetSnapshotGuardResult {
  bool ok = false;
  std::string_view reason = "budget snapshot validation failed";
};

inline bool try_compute_remaining(std::uint64_t max,
                                  std::uint64_t current,
                                  std::int64_t* out_remaining) {
  if (current <= max) {
    const auto diff = max - current;
    if (diff > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
      return false;
    }

    *out_remaining = static_cast<std::int64_t>(diff);
    return true;
  }

  const auto over = current - max;
  if (over > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    return false;
  }

  *out_remaining = -static_cast<std::int64_t>(over);
  return true;
}

inline constexpr std::size_t budget_type_index(BudgetType budget_type) {
  switch (budget_type) {
    case BudgetType::Token:
      return 0;
    case BudgetType::Turn:
      return 1;
    case BudgetType::ToolCall:
      return 2;
    case BudgetType::Latency:
      return 3;
    case BudgetType::Replan:
      return 4;
  }

  return 0;
}

// Validates T008 + T014 D4 frozen rules:
// 1) remaining must equal max-current for every entry.
// 2) reject_reason is only present when remaining is negative (over budget).
// 3) each budget_type appears at most once in one snapshot.
inline BudgetSnapshotGuardResult validate_budget_snapshot(const BudgetSnapshot& snapshot) {
  if (!snapshot.snapshot_at_ms.has_value()) {
    return BudgetSnapshotGuardResult{.ok = false, .reason = "snapshot_at_ms is required"};
  }

  if (snapshot.entries.empty()) {
    return BudgetSnapshotGuardResult{.ok = false, .reason = "at least one budget entry is required"};
  }

  std::array<bool, 5> seen_budget_types{false, false, false, false, false};
  for (const auto& entry : snapshot.entries) {
    const auto type_index = budget_type_index(entry.budget_type);
    if (seen_budget_types[type_index]) {
      return BudgetSnapshotGuardResult{.ok = false, .reason = "budget_type must be unique in snapshot"};
    }
    seen_budget_types[type_index] = true;

    std::int64_t computed_remaining = 0;
    if (!try_compute_remaining(entry.max, entry.current, &computed_remaining)) {
      return BudgetSnapshotGuardResult{.ok = false,
                                       .reason = "remaining computation overflow"};
    }

    if (entry.remaining != computed_remaining) {
      return BudgetSnapshotGuardResult{.ok = false,
                                       .reason = "remaining must equal max-current"};
    }

    const bool is_over_budget = entry.remaining < 0;
    const bool has_reject_reason = entry.reject_reason.has_value() && !entry.reject_reason->empty();

    if (is_over_budget && !has_reject_reason) {
      return BudgetSnapshotGuardResult{.ok = false,
                                       .reason = "reject_reason is required when over budget"};
    }

    if (!is_over_budget && has_reject_reason) {
      return BudgetSnapshotGuardResult{.ok = false,
                                       .reason = "reject_reason must be empty when not over budget"};
    }
  }

  return BudgetSnapshotGuardResult{.ok = true, .reason = "budget snapshot is valid"};
}

}  // namespace dasall::contracts
