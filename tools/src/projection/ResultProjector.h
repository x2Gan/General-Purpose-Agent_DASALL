#pragma once

#include "ToolInvocationContext.h"
#include "ToolInvocationEnvelope.h"
#include "route/ToolRouteSelector.h"
#include "tool/ToolResult.h"

namespace dasall::tools::projection {

class ResultProjector {
 public:
  [[nodiscard]] ToolInvocationEnvelope project(
      const contracts::ToolResult& result,
      const route::ToolRouteDecision& route_decision,
      const ToolInvocationContext& invocation_context) const;
  [[nodiscard]] ToolInvocationEnvelope project_success(
      const contracts::ToolResult& result,
      const route::ToolRouteDecision& route_decision,
      const ToolInvocationContext& invocation_context) const;
  [[nodiscard]] ToolInvocationEnvelope project_failure(
      const contracts::ToolResult& result,
      const route::ToolRouteDecision& route_decision,
      const ToolInvocationContext& invocation_context) const;
  [[nodiscard]] contracts::Observation build_observation(
      const contracts::ToolResult& result) const;
  [[nodiscard]] contracts::ObservationDigest build_digest(
      const contracts::ToolResult& result,
      const route::ToolRouteDecision& route_decision,
      const ToolInvocationContext& invocation_context) const;
};

}  // namespace dasall::tools::projection