#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "BuildProfileManifest.h"
#include "ICapabilityCache.h"
#include "IToolManager.h"
#include "config/ToolConfigAdapter.h"
#include "policy/ToolPolicyGate.h"
#include "registry/ToolRegistry.h"
#include "route/ToolRouteSelector.h"
#include "validation/ToolValidator.h"

namespace dasall::tools {

namespace ops {

class ToolMetricsBridge;

}  // namespace ops

struct CompensationRequest {
  std::optional<std::string> tool_call_id;
  std::optional<std::string> compensation_action;
  std::optional<std::string> target_ref;
  std::optional<std::string> reason_code;
  std::optional<std::vector<std::string>> evidence_refs;
};

struct ToolExecutionContext {
  ToolInvocationContext invocation_context;
  std::optional<std::string> lane_key;
};

namespace manager {

struct ToolExecutionRequest {
  const contracts::ToolIR& tool_ir;
  const route::ToolRouteDecision& route_decision;
  const ToolInvocationContext& invocation_context;
};

using ToolExecutor =
    std::function<contracts::ToolResult(const ToolExecutionRequest& execution_request)>;
using ToolProjector = std::function<ToolInvocationEnvelope(
    const contracts::ToolResult& result,
    const route::ToolRouteDecision& route_decision,
    const ToolInvocationContext& invocation_context)>;

struct ToolAuditHooks {
  std::function<void(const contracts::ToolRequest&, const ToolInvocationContext&)> on_requested;
  std::function<void(const ToolInvocationEnvelope&)> on_completed;
  std::function<void(const ToolInvocationEnvelope&)> on_failed;
  std::function<void(const CompensationRequest&, const ToolInvocationEnvelope&)>
      on_compensation;
};

struct ToolManagerDependencies {
  std::shared_ptr<registry::ToolRegistry> registry;
  std::shared_ptr<validation::ToolValidator> validator;
  std::shared_ptr<config::ToolConfigAdapter> config_adapter;
  std::shared_ptr<IPolicyGate> policy_gate;
  std::shared_ptr<route::ToolRouteSelector> route_selector;
  std::shared_ptr<ops::ToolMetricsBridge> metrics_bridge;
  profiles::BuildProfileManifest build_manifest;
  ToolExecutor executor;
  ToolProjector projector;
  ToolAuditHooks audit_hooks;
};

}  // namespace manager

class ToolManager final : public IToolManager {
 public:
  ToolManager();
  explicit ToolManager(manager::ToolManagerDependencies dependencies);

  [[nodiscard]] ToolInvocationEnvelope invoke(
      const contracts::ToolRequest& request,
      const ToolInvocationContext& context) override;
  [[nodiscard]] std::vector<ToolInvocationEnvelope> invoke_batch(
      std::span<const contracts::ToolRequest> requests,
      const ToolInvocationContext& context) override;
  [[nodiscard]] ToolInvocationEnvelope compensate(
      const CompensationRequest& request,
      const ToolInvocationContext& context) override;

  void set_route_health(route::ToolRouteHealthSnapshot health);
  void set_capability_snapshot(std::optional<CapabilitySnapshot> capability_snapshot);

 private:
  [[nodiscard]] ToolInvocationEnvelope run_invoke_pipeline(
      const contracts::ToolRequest& request,
      const ToolInvocationContext& context) const;
  [[nodiscard]] ToolInvocationEnvelope run_compensation_pipeline(
      const CompensationRequest& request,
      const ToolInvocationContext& context) const;
  [[nodiscard]] static manager::ToolManagerDependencies default_dependencies();

  manager::ToolManagerDependencies dependencies_;
  route::ToolRouteHealthSnapshot route_health_{};
  std::optional<CapabilitySnapshot> capability_snapshot_;
};

}  // namespace dasall::tools