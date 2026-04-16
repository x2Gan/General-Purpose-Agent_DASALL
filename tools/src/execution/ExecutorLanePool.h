#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "config/ToolConfigAdapter.h"
#include "tool/ToolIR.h"

namespace dasall::tools::execution {

enum class ExecutorLaneKind {
  Unavailable = 0,
  Builtin = 1,
  Workflow = 2,
  MCP = 3,
};

struct LaneReservation {
  ExecutorLaneKind kind = ExecutorLaneKind::Unavailable;
  std::string lane_key;
  bool available = false;
  std::uint32_t concurrency_budget = 0U;
};

class ExecutorLanePool {
 public:
  [[nodiscard]] LaneReservation reserve_builtin(
      const config::ToolTimeoutView& timeout_view) const;
  [[nodiscard]] LaneReservation reserve_workflow(
      const config::ToolTimeoutView& timeout_view) const;
  [[nodiscard]] LaneReservation reserve_mcp(
      std::string_view server_id,
      const config::ToolTimeoutView& timeout_view) const;
};

}  // namespace dasall::tools::execution