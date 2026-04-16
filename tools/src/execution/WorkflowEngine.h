#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "ToolInvocationEnvelope.h"
#include "tool/ToolIR.h"
#include "tool/ToolResult.h"

namespace dasall::tools {

struct ToolExecutionContext;

namespace execution {

enum class WorkflowStepKind {
  Tool = 0,
  Delegation = 1,
};

enum class WorkflowDependencyKind {
  Payload = 0,
  Ordering = 1,
};

enum class WorkflowDelegationMode {
  Disabled = 0,
  RecommendOnly = 1,
};

struct WorkflowEdge {
  std::string from_step_id;
  std::string to_step_id;
  WorkflowDependencyKind dependency_kind = WorkflowDependencyKind::Payload;
};

struct WorkflowStepOutputBinding {
  std::string source_step_id;
  std::string source_json_pointer;
  std::string target_step_id;
  std::string target_argument_key;
  bool required = true;
};

struct WorkflowDelegationPolicy {
  WorkflowDelegationMode mode = WorkflowDelegationMode::Disabled;
  std::uint32_t max_delegate_steps = 0U;
};

struct WorkflowStep {
  std::string step_id;
  contracts::ToolIR tool_ir;
  std::optional<contracts::ToolIRRoute> route_kind_hint;
  WorkflowStepKind step_kind = WorkflowStepKind::Tool;
  std::vector<std::string> depends_on;
  bool allow_partial_side_effect = false;
  std::optional<std::uint32_t> timeout_override_ms;
  std::optional<std::string> delegate_target;
};

struct WorkflowPlan {
  std::string workflow_id;
  std::vector<std::string> entry_step_ids;
  std::vector<WorkflowStep> steps;
  std::vector<WorkflowEdge> edges;
  std::vector<WorkflowStepOutputBinding> step_output_mapping;
  WorkflowDelegationPolicy delegation_policy;
  std::vector<std::pair<std::string, std::string>> metadata;
};

struct WorkflowStepReceipt {
  std::string step_id;
  std::string route_kind;
  contracts::ToolResult result;
  std::vector<std::string> mapped_arguments;
  std::vector<std::string> evidence_refs;
};

struct WorkflowDelegationSidecar {
  std::string step_id;
  std::string delegate_target;
  std::string reason_code;
  std::vector<std::string> input_evidence_refs;
};

struct WorkflowReceipt {
  std::string workflow_id;
  std::string status;
  std::vector<std::string> completed_step_ids;
  std::vector<std::string> skipped_step_ids;
  std::optional<std::string> failed_step_id;
  std::vector<WorkflowStepReceipt> step_results;
  std::vector<ToolCompensationHint> compensation_hints;
  std::vector<std::string> evidence_refs;
  std::optional<contracts::ErrorInfo> failure_digest;
  std::optional<WorkflowDelegationSidecar> delegation_sidecar;
};

struct WorkflowBatch {
  std::vector<std::string> step_ids;
};

struct WorkflowBatchBuildResult {
  bool ok = false;
  std::vector<WorkflowBatch> batches;
  std::optional<contracts::ErrorInfo> error;
  std::vector<std::string> evidence_refs;
  std::string failure_reason_code;
};

struct WorkflowStepDispatchResult {
  bool ok = false;
  std::string step_id;
  std::string route_kind;
  contracts::ToolResult result;
  std::vector<std::string> mapped_arguments;
  std::vector<std::string> evidence_refs;
  std::optional<WorkflowDelegationSidecar> delegation_sidecar;
  std::string failure_reason_code;
};

struct WorkflowExecutionOutcome {
  WorkflowReceipt receipt;
  contracts::ToolResult tool_result;
  std::optional<std::vector<ToolCompensationHint>> compensation_hints;
  std::vector<std::string> evidence_refs;
  std::string failure_reason_code;
};

using WorkflowPlanLoader = std::function<std::optional<WorkflowPlan>(
    const contracts::ToolIR& tool_ir,
    std::string& failure_reason_code,
    std::string& failure_message)>;

using WorkflowStepExecutor = std::function<contracts::ToolResult(
    const contracts::ToolIR& tool_ir,
    const ToolExecutionContext& execution_context)>;

struct WorkflowEngineDependencies {
  WorkflowPlanLoader plan_loader;
  WorkflowStepExecutor builtin_executor;
  WorkflowStepExecutor mcp_executor;
};

class WorkflowEngine {
 public:
  WorkflowEngine();
  explicit WorkflowEngine(WorkflowEngineDependencies dependencies);

  [[nodiscard]] WorkflowExecutionOutcome execute(
      const contracts::ToolIR& workflow_ir,
      const ToolExecutionContext& execution_context) const;
  [[nodiscard]] WorkflowExecutionOutcome execute(
      const WorkflowPlan& plan,
      const contracts::ToolIR& workflow_ir,
      const ToolExecutionContext& execution_context) const;
  [[nodiscard]] WorkflowBatchBuildResult build_batches(
      const WorkflowPlan& plan) const;
  [[nodiscard]] WorkflowStepDispatchResult dispatch_step(
      const WorkflowPlan& plan,
      const WorkflowStep& step,
      const ToolExecutionContext& execution_context,
      const std::vector<WorkflowStepReceipt>& prior_results,
      std::uint32_t delegation_count) const;
  void collect_step_result(
      WorkflowReceipt& receipt,
      const WorkflowStep& step,
      WorkflowStepDispatchResult dispatch_result) const;
  [[nodiscard]] WorkflowExecutionOutcome finalize_receipt(
      WorkflowReceipt receipt,
      const contracts::ToolIR& workflow_ir) const;

 private:
  [[nodiscard]] static WorkflowEngineDependencies default_dependencies();

  WorkflowEngineDependencies dependencies_;
};

}  // namespace execution
}  // namespace dasall::tools