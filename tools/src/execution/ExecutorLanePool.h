#pragma once

#include <cstdint>
#include <memory>
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

struct LaneAcquireResult {
  LaneReservation reservation;
  bool acquired = false;
  std::string reason_code;
  std::uint32_t inflight = 0U;
};

class ExecutorLanePool {
 public:
  ExecutorLanePool();

  [[nodiscard]] LaneReservation reserve_builtin(
      const config::ToolTimeoutView& timeout_view) const;
  [[nodiscard]] LaneReservation reserve_workflow(
      const config::ToolTimeoutView& timeout_view) const;
  [[nodiscard]] LaneReservation reserve_mcp(
      std::string_view server_id,
      const config::ToolTimeoutView& timeout_view) const;
  [[nodiscard]] LaneAcquireResult acquire_builtin(
    const config::ToolTimeoutView& timeout_view) const;
  [[nodiscard]] LaneAcquireResult acquire_workflow(
    const config::ToolTimeoutView& timeout_view) const;
  [[nodiscard]] LaneAcquireResult acquire_mcp(
    std::string_view server_id,
    const config::ToolTimeoutView& timeout_view) const;
  void release(std::string_view lane_key) const;

 private:
  struct SharedState;

  [[nodiscard]] LaneAcquireResult acquire(
    const LaneReservation& reservation,
    std::string reason_code) const;

  std::shared_ptr<SharedState> state_;
};

}  // namespace dasall::tools::execution