#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dasall::contracts {

// BudgetType freezes the five-dimensional budget view aligned with T007/T008.
enum class BudgetType {
  Token,
  Turn,
  ToolCall,
  Latency,
  Replan,
};

// BudgetSnapshotEntry captures one budget dimension state at snapshot time.
// remaining is expected to be derived from max-current and can be negative
// when the dimension is already over budget.
struct BudgetSnapshotEntry {
  BudgetType budget_type = BudgetType::Token;
  std::uint64_t current = 0;
  std::uint64_t max = 0;
  std::int64_t remaining = 0;
  std::optional<std::string> reject_reason;
};

// BudgetSnapshot is a state description, not an execution command.
// The guard enforces consistency across entries and reject_reason semantics.
struct BudgetSnapshot {
  std::optional<std::uint64_t> snapshot_at_ms;
  std::vector<BudgetSnapshotEntry> entries;
  std::optional<std::string> overall_reject_reason;
};

}  // namespace dasall::contracts
