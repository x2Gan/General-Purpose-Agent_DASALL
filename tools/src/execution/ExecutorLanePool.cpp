#include "ExecutorLanePool.h"

#include <mutex>
#include <unordered_map>
#include <utility>

namespace {

struct LaneCounters {
  std::uint32_t concurrency_budget = 0U;
  std::uint32_t inflight = 0U;
};

[[nodiscard]] std::string backpressure_reason_code(
    dasall::tools::execution::ExecutorLaneKind kind) {
  switch (kind) {
    case dasall::tools::execution::ExecutorLaneKind::Builtin:
      return "lane.builtin.backpressure";
    case dasall::tools::execution::ExecutorLaneKind::Workflow:
      return "lane.workflow.backpressure";
    case dasall::tools::execution::ExecutorLaneKind::MCP:
      return "lane.mcp.backpressure";
    case dasall::tools::execution::ExecutorLaneKind::Unavailable:
      break;
  }

  return "lane.unavailable";
}

}  // namespace

namespace dasall::tools::execution {

struct ExecutorLanePool::SharedState {
  std::mutex mutex;
  std::unordered_map<std::string, LaneCounters> counters_by_lane;
};

ExecutorLanePool::ExecutorLanePool()
    : state_(std::make_shared<SharedState>()) {}

LaneReservation ExecutorLanePool::reserve_builtin(
    const config::ToolTimeoutView& timeout_view) const {
  const bool available = timeout_view.builtin_lane_enabled &&
                         timeout_view.max_tool_calls > 0U &&
                         timeout_view.tool.timeout_ms > 0;
  return LaneReservation{
      .kind = available ? ExecutorLaneKind::Builtin : ExecutorLaneKind::Unavailable,
      .lane_key = "builtin",
      .available = available,
      .concurrency_budget = available ? timeout_view.max_tool_calls : 0U,
  };
}

LaneAcquireResult ExecutorLanePool::acquire_builtin(
    const config::ToolTimeoutView& timeout_view) const {
  return acquire(
      reserve_builtin(timeout_view),
      backpressure_reason_code(ExecutorLaneKind::Builtin));
}

LaneReservation ExecutorLanePool::reserve_workflow(
    const config::ToolTimeoutView& timeout_view) const {
  const bool available = timeout_view.max_tool_calls > 0U &&
                         timeout_view.workflow.timeout_ms > 0;
  return LaneReservation{
      .kind = available ? ExecutorLaneKind::Workflow : ExecutorLaneKind::Unavailable,
      .lane_key = "workflow",
      .available = available,
      .concurrency_budget = available ? timeout_view.max_tool_calls : 0U,
  };
}

LaneAcquireResult ExecutorLanePool::acquire_workflow(
    const config::ToolTimeoutView& timeout_view) const {
  return acquire(
      reserve_workflow(timeout_view),
      backpressure_reason_code(ExecutorLaneKind::Workflow));
}

LaneReservation ExecutorLanePool::reserve_mcp(
    std::string_view server_id,
    const config::ToolTimeoutView& timeout_view) const {
  const bool available = timeout_view.mcp_lane_enabled &&
                         timeout_view.max_tool_calls > 0U &&
                         timeout_view.mcp.timeout_ms > 0 &&
                         !server_id.empty();
  return LaneReservation{
      .kind = available ? ExecutorLaneKind::MCP : ExecutorLaneKind::Unavailable,
      .lane_key = available ? std::string("mcp:") + std::string(server_id)
                            : std::string("mcp"),
      .available = available,
      .concurrency_budget = available ? timeout_view.max_tool_calls : 0U,
  };
}

LaneAcquireResult ExecutorLanePool::acquire_mcp(
    std::string_view server_id,
    const config::ToolTimeoutView& timeout_view) const {
  return acquire(
      reserve_mcp(server_id, timeout_view),
      backpressure_reason_code(ExecutorLaneKind::MCP));
}

void ExecutorLanePool::release(std::string_view lane_key) const {
  if (!state_ || lane_key.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(state_->mutex);
  const auto found = state_->counters_by_lane.find(std::string(lane_key));
  if (found == state_->counters_by_lane.end() || found->second.inflight == 0U) {
    return;
  }

  --found->second.inflight;
}

LaneAcquireResult ExecutorLanePool::acquire(
    const LaneReservation& reservation,
    std::string reason_code) const {
  LaneAcquireResult result{
      .reservation = reservation,
      .acquired = false,
      .reason_code = std::move(reason_code),
      .inflight = 0U,
  };
  if (!state_ || !reservation.available || reservation.concurrency_budget == 0U ||
      reservation.lane_key.empty()) {
    return result;
  }

  std::lock_guard<std::mutex> lock(state_->mutex);
  auto& counters = state_->counters_by_lane[reservation.lane_key];
  counters.concurrency_budget = reservation.concurrency_budget;
  result.inflight = counters.inflight;
  if (counters.inflight >= counters.concurrency_budget) {
    return result;
  }

  ++counters.inflight;
  result.acquired = true;
  result.reason_code.clear();
  result.inflight = counters.inflight;
  return result;
}

}  // namespace dasall::tools::execution