#include "ExecutorLanePool.h"

namespace dasall::tools::execution {

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

}  // namespace dasall::tools::execution