#pragma once

#include <optional>
#include <string>
#include <vector>

#include "ICapabilityCache.h"
#include "config/ToolConfigAdapter.h"
#include "execution/ExecutorLanePool.h"
#include "mcp/IMCPAdapter.h"
#include "tool/ToolDescriptor.h"
#include "tool/ToolIR.h"

namespace dasall::tools::route {

struct ToolRouteHealthSnapshot {
  bool builtin_lane_healthy = true;
  bool workflow_lane_healthy = true;
  bool mcp_lane_healthy = true;
};

struct ToolRouteDecision {
  bool available = false;
  contracts::ToolIRRoute route = contracts::ToolIRRoute::Unspecified;
  std::string lane_key;
  std::string reason_code;
  bool uses_stale_snapshot = false;
  std::optional<std::string> server_id;
};

class ToolRouteSelector {
 public:
  ToolRouteSelector() = default;
  explicit ToolRouteSelector(execution::ExecutorLanePool lane_pool);

  [[nodiscard]] ToolRouteDecision select_route(
      const contracts::ToolIR& tool_ir,
      const contracts::ToolDescriptor& descriptor,
      const config::ToolTimeoutView& timeout_view,
      const std::vector<mcp::MCPToolBinding>& bindings,
      const std::optional<tools::CapabilitySnapshot>& capability_snapshot,
      const ToolRouteHealthSnapshot& health) const;
  [[nodiscard]] int score_builtin_candidate(
      const contracts::ToolIR& tool_ir,
      const contracts::ToolDescriptor& descriptor,
      const config::ToolTimeoutView& timeout_view,
      const ToolRouteHealthSnapshot& health) const;
  [[nodiscard]] int score_mcp_candidate(
      const contracts::ToolIR& tool_ir,
      const config::ToolTimeoutView& timeout_view,
      const std::vector<mcp::MCPToolBinding>& bindings,
      const std::optional<tools::CapabilitySnapshot>& capability_snapshot,
      const ToolRouteHealthSnapshot& health) const;
  [[nodiscard]] ToolRouteDecision select_workflow_route(
      const contracts::ToolDescriptor& descriptor,
      const config::ToolTimeoutView& timeout_view,
      const ToolRouteHealthSnapshot& health) const;

 private:
  [[nodiscard]] static bool is_workflow_like(const contracts::ToolDescriptor& descriptor);
  [[nodiscard]] static bool is_agent_delegation(const contracts::ToolDescriptor& descriptor);
  [[nodiscard]] static bool snapshot_supports_binding(
      const tools::CapabilitySnapshot& snapshot,
      const mcp::MCPToolBinding& binding);
  [[nodiscard]] static bool may_use_stale_snapshot(
      const config::ToolTimeoutView& timeout_view,
      const tools::CapabilitySnapshot& snapshot);
  [[nodiscard]] static bool prefers_mcp(const contracts::ToolIR& tool_ir);
  [[nodiscard]] ToolRouteDecision build_builtin_decision(
      const config::ToolTimeoutView& timeout_view) const;
  [[nodiscard]] ToolRouteDecision build_mcp_decision(
      const mcp::MCPToolBinding& binding,
      const config::ToolTimeoutView& timeout_view,
      bool uses_stale_snapshot) const;
  [[nodiscard]] static ToolRouteDecision unavailable(std::string reason_code);

  execution::ExecutorLanePool lane_pool_;
};

}  // namespace dasall::tools::route