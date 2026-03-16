#pragma once

#include <cstdint>
#include <optional>

namespace dasall::contracts {

// RuntimeBudget freezes the WP02-T007 five-dimensional runtime budget surface.
// Fields stay optional at the data-structure layer so guards can detect and
// report missing required values with precise reasons.
struct RuntimeBudget {
  // Maximum token budget for one request/session execution scope.
  std::optional<std::uint32_t> max_tokens;

  // Maximum main-loop turn budget (unit: turn count).
  std::optional<std::uint32_t> max_turns;

  // Maximum tool invocation budget (unit: call count).
  std::optional<std::uint32_t> max_tool_calls;

  // Maximum end-to-end latency budget (unit: milliseconds).
  std::optional<std::uint32_t> max_latency_ms;

  // Maximum replan budget (unit: replan count).
  std::optional<std::uint32_t> max_replan_count;
};

}  // namespace dasall::contracts
