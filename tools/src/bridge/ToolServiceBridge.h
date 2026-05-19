#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "ServiceTypes.h"
#include "ToolInvocationContext.h"
#include "tool/ToolIR.h"

namespace dasall::tools {

struct CompensationRequest;

}  // namespace dasall::tools

namespace dasall::tools::bridge {

struct CompensationTargetRef {
  std::string capability_id;
  std::string target_id;
};

[[nodiscard]] std::optional<CompensationTargetRef> resolve_compensation_target(
    std::string_view target_ref);
[[nodiscard]] std::string format_compensation_target_ref(
    std::string_view capability_id,
    std::string_view target_id = {});

class ToolServiceBridge {
 public:
  [[nodiscard]] services::ServiceCallContext build_context(
      const contracts::ToolIR& tool_ir,
      const ToolInvocationContext& invocation_context) const;
  [[nodiscard]] services::ExecutionCompensationRequest build_compensation_request(
      const contracts::ToolIR& tool_ir,
      const CompensationRequest& request,
      const ToolInvocationContext& invocation_context) const;
  [[nodiscard]] services::ExecutionCommandRequest build_action_request(
      const contracts::ToolIR& tool_ir,
      const ToolInvocationContext& invocation_context) const;
  [[nodiscard]] services::DataQueryRequest build_query_request(
      const contracts::ToolIR& tool_ir,
      const ToolInvocationContext& invocation_context) const;
  [[nodiscard]] services::ExecutionDiagnoseRequest build_diagnose_request(
      const contracts::ToolIR& tool_ir,
      const ToolInvocationContext& invocation_context) const;

 private:
  [[nodiscard]] static std::string resolve_request_id(const contracts::ToolIR& tool_ir);
  [[nodiscard]] static std::string resolve_tool_call_id(const contracts::ToolIR& tool_ir,
                                                        std::string_view request_id);
  [[nodiscard]] static std::string resolve_tool_name(const contracts::ToolIR& tool_ir);
  [[nodiscard]] static std::string resolve_session_id(
      const ToolInvocationContext& invocation_context,
      std::string_view request_id);
  [[nodiscard]] static std::string resolve_trace_id(
      const ToolInvocationContext& invocation_context,
      std::string_view tool_call_id);
  [[nodiscard]] static std::string resolve_goal_id(const contracts::ToolIR& tool_ir,
                                                   std::string_view tool_call_id);
  [[nodiscard]] static std::uint64_t resolve_deadline_ms(
      const contracts::ToolIR& tool_ir,
      const ToolInvocationContext& invocation_context);
  [[nodiscard]] static std::optional<contracts::RuntimeBudget> resolve_budget_guard(
      const ToolInvocationContext& invocation_context);
  [[nodiscard]] static services::ServiceDataFreshness resolve_query_freshness(
      const ToolInvocationContext& invocation_context);
  [[nodiscard]] static services::CapabilityTargetRef build_target(
      const contracts::ToolIR& tool_ir);
};

}  // namespace dasall::tools::bridge