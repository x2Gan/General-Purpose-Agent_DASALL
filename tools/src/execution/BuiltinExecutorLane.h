#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "IDataService.h"
#include "IExecutionService.h"
#include "ToolManager.h"
#include "bridge/ToolServiceBridge.h"
#include "registry/ToolRegistry.h"

namespace dasall::tools::execution {

struct BuiltinExecutorLaneDependencies {
  std::shared_ptr<registry::ToolRegistry> registry;
  std::shared_ptr<bridge::ToolServiceBridge> service_bridge;
  std::shared_ptr<services::IExecutionService> execution_service;
  std::shared_ptr<services::IDataService> data_service;
  std::function<std::int64_t()> now_ms;
};

class BuiltinExecutorLane {
 public:
  BuiltinExecutorLane();
  explicit BuiltinExecutorLane(BuiltinExecutorLaneDependencies dependencies);

  [[nodiscard]] contracts::ToolResult execute(
      const contracts::ToolIR& tool_ir,
      const ToolExecutionContext& execution_context) const;
  [[nodiscard]] contracts::ToolResult dispatch_action(
      const contracts::ToolIR& tool_ir,
      const ToolExecutionContext& execution_context) const;
  [[nodiscard]] contracts::ToolResult dispatch_compensation(
      const contracts::ToolIR& tool_ir,
      const CompensationRequest& request,
      const ToolExecutionContext& execution_context) const;
  [[nodiscard]] contracts::ToolResult dispatch_query(
      const contracts::ToolIR& tool_ir,
      const ToolExecutionContext& execution_context) const;
  [[nodiscard]] contracts::ToolResult dispatch_diagnose(
      const contracts::ToolIR& tool_ir,
      const ToolExecutionContext& execution_context) const;

 private:
  [[nodiscard]] contracts::ToolResult map_service_result(
      const contracts::ToolIR& tool_ir,
      const ToolExecutionContext& execution_context,
      std::string dispatch_kind,
      const services::ExecutionCommandResult& result) const;
  [[nodiscard]] contracts::ToolResult build_compensation_failure_result(
      const contracts::ToolIR& tool_ir,
      const ToolExecutionContext& execution_context,
      std::string message,
      std::string stage) const;
  [[nodiscard]] contracts::ToolResult map_service_result(
      const contracts::ToolIR& tool_ir,
      const ToolExecutionContext& execution_context,
      const services::DataQueryResult& result) const;
  [[nodiscard]] contracts::ToolResult map_service_result(
      const contracts::ToolIR& tool_ir,
      const ToolExecutionContext& execution_context,
      const services::ExecutionDiagnoseResult& result) const;
  [[nodiscard]] static BuiltinExecutorLaneDependencies default_dependencies();

  BuiltinExecutorLaneDependencies dependencies_;
};

}  // namespace dasall::tools::execution