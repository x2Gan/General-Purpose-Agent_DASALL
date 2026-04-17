#include "ToolRouteSelector.h"

#include <algorithm>

namespace dasall::tools::route {

namespace {

constexpr int kBuiltinBaseScore = 60;
constexpr int kBuiltinRouteHintBonus = 20;
constexpr int kBuiltinReadOnlyBonus = 5;

constexpr int kMCPBaseScore = 50;
constexpr int kMCPRoutePreferenceBonus = 25;
constexpr int kMCPFreshSnapshotBonus = 15;

}  // namespace

ToolRouteSelector::ToolRouteSelector(execution::ExecutorLanePool lane_pool)
    : lane_pool_(std::move(lane_pool)) {}

ToolRouteDecision ToolRouteSelector::select_route(
    const contracts::ToolIR& tool_ir,
    const contracts::ToolDescriptor& descriptor,
    const config::ToolTimeoutView& timeout_view,
    const std::vector<mcp::MCPToolBinding>& bindings,
    const std::optional<tools::CapabilitySnapshot>& capability_snapshot,
    const ToolRouteHealthSnapshot& health) const {
  if (is_workflow_like(descriptor)) {
    return select_workflow_route(descriptor, timeout_view, health);
  }

  const auto builtin_score = score_builtin_candidate(tool_ir, descriptor, timeout_view, health);
  const auto mcp_score =
      score_mcp_candidate(tool_ir, timeout_view, bindings, capability_snapshot, health);

  if (builtin_score <= 0 && mcp_score <= 0) {
    return unavailable("RouteUnavailable");
  }

  if (mcp_score > builtin_score) {
    const auto selected_binding = std::find_if(
        bindings.begin(), bindings.end(), [&](const mcp::MCPToolBinding& binding) {
          return capability_snapshot.has_value() &&
                 binding.server_id == capability_snapshot->server_id &&
                 snapshot_supports_binding(*capability_snapshot, binding);
        });
    if (selected_binding != bindings.end()) {
      return build_mcp_decision(
          *selected_binding,
          timeout_view,
          capability_snapshot->freshness == tools::CapabilityFreshness::stale);
    }
  }

  if (builtin_score > 0) {
    return build_builtin_decision(timeout_view);
  }

  if (mcp_score > 0 && capability_snapshot.has_value()) {
    const auto selected_binding = std::find_if(
        bindings.begin(), bindings.end(), [&](const mcp::MCPToolBinding& binding) {
          return binding.server_id == capability_snapshot->server_id &&
                 snapshot_supports_binding(*capability_snapshot, binding);
        });
    if (selected_binding != bindings.end()) {
      return build_mcp_decision(
          *selected_binding,
          timeout_view,
          capability_snapshot->freshness == tools::CapabilityFreshness::stale);
    }
  }

  return unavailable("RouteUnavailable");
}

int ToolRouteSelector::score_builtin_candidate(
    const contracts::ToolIR& tool_ir,
    const contracts::ToolDescriptor& descriptor,
    const config::ToolTimeoutView& timeout_view,
    const ToolRouteHealthSnapshot& health) const {
  if (!timeout_view.builtin_lane_enabled || !health.builtin_lane_healthy) {
    return 0;
  }
  if (!descriptor.category.has_value() || is_workflow_like(descriptor)) {
    return 0;
  }

  auto score = kBuiltinBaseScore;
  if (tool_ir.route.has_value() && *tool_ir.route == contracts::ToolIRRoute::LocalTool) {
    score += kBuiltinRouteHintBonus;
  }
  if (descriptor.is_read_only.value_or(false)) {
    score += kBuiltinReadOnlyBonus;
  }

  return score;
}

int ToolRouteSelector::score_mcp_candidate(
    const contracts::ToolIR& tool_ir,
    const config::ToolTimeoutView& timeout_view,
    const std::vector<mcp::MCPToolBinding>& bindings,
    const std::optional<tools::CapabilitySnapshot>& capability_snapshot,
    const ToolRouteHealthSnapshot& health) const {
  if (!timeout_view.mcp_lane_enabled || !health.mcp_lane_healthy || bindings.empty() ||
      !capability_snapshot.has_value()) {
    return 0;
  }

  if (capability_snapshot->freshness == tools::CapabilityFreshness::expired) {
    return 0;
  }
  if (capability_snapshot->freshness == tools::CapabilityFreshness::stale &&
      !may_use_stale_snapshot(timeout_view, *capability_snapshot)) {
    return 0;
  }

  const auto supported_binding = std::find_if(
      bindings.begin(), bindings.end(), [&](const mcp::MCPToolBinding& binding) {
        return binding.server_id == capability_snapshot->server_id &&
               snapshot_supports_binding(*capability_snapshot, binding);
      });
  if (supported_binding == bindings.end()) {
    return 0;
  }

  auto score = kMCPBaseScore;
  if (prefers_mcp(tool_ir)) {
    score += kMCPRoutePreferenceBonus;
  }
  if (capability_snapshot->freshness == tools::CapabilityFreshness::fresh) {
    score += kMCPFreshSnapshotBonus;
  }

  return score;
}

ToolRouteDecision ToolRouteSelector::select_workflow_route(
    const contracts::ToolDescriptor& descriptor,
    const config::ToolTimeoutView& timeout_view,
    const ToolRouteHealthSnapshot& health) const {
  if (!health.workflow_lane_healthy) {
    return unavailable("RouteUnavailable");
  }
  if (is_agent_delegation(descriptor) && !timeout_view.multi_agent_enabled) {
    return unavailable("route.agent_delegation_disabled");
  }

  const auto reservation = lane_pool_.reserve_workflow(timeout_view);
  if (!reservation.available) {
    return unavailable("RouteUnavailable");
  }

  return ToolRouteDecision{
      .available = true,
      .route = contracts::ToolIRRoute::WorkflowEngine,
      .lane_key = reservation.lane_key,
      .reason_code = "route.workflow.selected",
      .uses_stale_snapshot = false,
      .server_id = std::nullopt,
  };
}

bool ToolRouteSelector::is_workflow_like(const contracts::ToolDescriptor& descriptor) {
  return descriptor.category.has_value() &&
         (*descriptor.category == contracts::ToolCategory::Workflow ||
          *descriptor.category == contracts::ToolCategory::AgentDelegation);
}

bool ToolRouteSelector::is_agent_delegation(const contracts::ToolDescriptor& descriptor) {
  return descriptor.category.has_value() &&
         *descriptor.category == contracts::ToolCategory::AgentDelegation;
}

bool ToolRouteSelector::snapshot_supports_binding(
    const tools::CapabilitySnapshot& snapshot,
    const mcp::MCPToolBinding& binding) {
  return std::any_of(
      snapshot.entries.begin(), snapshot.entries.end(), [&](const tools::CapabilityEntry& entry) {
        const bool capability_match = !binding.remote_capability_id.has_value() ||
                                      *binding.remote_capability_id == entry.capability_id;
        const bool tool_match = std::find(
                                    entry.tool_names.begin(),
                                    entry.tool_names.end(),
                                    binding.remote_tool_name) != entry.tool_names.end();
        return capability_match && tool_match;
      });
}

bool ToolRouteSelector::may_use_stale_snapshot(
    const config::ToolTimeoutView& timeout_view,
    const tools::CapabilitySnapshot& snapshot) {
  return timeout_view.stale_read_allowed && snapshot.trust_marker.has_value() &&
         !snapshot.trust_marker->empty();
}

bool ToolRouteSelector::prefers_mcp(const contracts::ToolIR& tool_ir) {
  return tool_ir.route.has_value() && *tool_ir.route == contracts::ToolIRRoute::MCPRemote;
}

ToolRouteDecision ToolRouteSelector::build_builtin_decision(
    const config::ToolTimeoutView& timeout_view) const {
  const auto reservation = lane_pool_.reserve_builtin(timeout_view);
  if (!reservation.available) {
    return unavailable("RouteUnavailable");
  }

  return ToolRouteDecision{
      .available = true,
      .route = contracts::ToolIRRoute::LocalTool,
      .lane_key = reservation.lane_key,
      .reason_code = "route.builtin.selected",
      .uses_stale_snapshot = false,
      .server_id = std::nullopt,
  };
}

ToolRouteDecision ToolRouteSelector::build_mcp_decision(
    const mcp::MCPToolBinding& binding,
    const config::ToolTimeoutView& timeout_view,
    bool uses_stale_snapshot) const {
  const auto reservation = lane_pool_.reserve_mcp(binding.server_id, timeout_view);
  if (!reservation.available) {
    return unavailable("RouteUnavailable");
  }

  return ToolRouteDecision{
      .available = true,
      .route = contracts::ToolIRRoute::MCPRemote,
      .lane_key = reservation.lane_key,
      .reason_code = uses_stale_snapshot ? "route.mcp.stale_fallback"
                                         : "route.mcp.selected",
      .uses_stale_snapshot = uses_stale_snapshot,
      .server_id = binding.server_id,
  };
}

ToolRouteDecision ToolRouteSelector::unavailable(std::string reason_code) {
  return ToolRouteDecision{
      .available = false,
      .route = contracts::ToolIRRoute::Unspecified,
      .lane_key = "unavailable",
      .reason_code = std::move(reason_code),
      .uses_stale_snapshot = false,
      .server_id = std::nullopt,
  };
}

}  // namespace dasall::tools::route