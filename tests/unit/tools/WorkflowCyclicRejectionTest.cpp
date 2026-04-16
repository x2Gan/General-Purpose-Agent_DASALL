#include <exception>
#include <iostream>

#include "RuntimePolicySnapshot.h"
#include "ToolManager.h"
#include "execution/WorkflowEngine.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::profiles::RuntimePolicySnapshot make_snapshot() {
  return dasall::profiles::RuntimePolicySnapshot{
      1U,
      "desktop_full",
      dasall::contracts::RuntimeBudget{},
      dasall::profiles::ModelProfile{},
      dasall::profiles::TokenBudgetPolicy{},
      dasall::profiles::PromptPolicy{},
      dasall::profiles::CapabilityCachePolicy{},
      dasall::profiles::DegradePolicy{},
      dasall::profiles::TimeoutPolicy{},
      dasall::profiles::ExecutionPolicy{
          .requires_high_risk_confirmation = false,
          .safe_mode_enabled = true,
          .audit_level = "full",
          .allowed_tool_domains = {"workflow", "builtin"},
      },
      dasall::profiles::OpsPolicy{},
      4U};
}

[[nodiscard]] dasall::tools::ToolExecutionContext make_execution_context(
    const dasall::profiles::RuntimePolicySnapshot& snapshot) {
  return dasall::tools::ToolExecutionContext{
      .invocation_context = dasall::tools::ToolInvocationContext{
          .caller_domain = std::string("runtime.main"),
          .session_id = std::string("session-workflow-cycle"),
          .profile_snapshot = &snapshot,
          .trace = {
            .trace_id = std::nullopt,
            .span_id = std::nullopt,
            .parent_span_id = std::nullopt,
          },
          .confirmation_facts = std::nullopt,
      },
      .lane_key = std::string("workflow"),
  };
}

[[nodiscard]] dasall::contracts::ToolIR make_ir(std::string tool_call_id) {
  return dasall::contracts::ToolIR{
      .request_id = std::string("req-cycle"),
      .tool_call_id = std::move(tool_call_id),
      .tool_name = std::string("workflow.cycle"),
      .operation = dasall::contracts::ToolIROperation::Invoke,
      .normalized_arguments = std::string("{}"),
      .route = dasall::contracts::ToolIRRoute::LocalTool,
      .timeout_ms = 1000U,
      .idempotency_key = std::string("idem-cycle"),
      .priority = dasall::contracts::ToolIRPriority::Normal,
      .goal_id = std::string("goal-cycle"),
      .worker_task_id = std::string("worker-cycle"),
  };
}

void test_workflow_engine_rejects_cyclic_graphs() {
  const auto snapshot = make_snapshot();
  const auto execution_context = make_execution_context(snapshot);
  const dasall::tools::execution::WorkflowPlan plan{
      .workflow_id = std::string("workflow.cycle"),
      .entry_step_ids = {"step-a"},
      .steps = {
          dasall::tools::execution::WorkflowStep{
              .step_id = "step-a",
              .tool_ir = make_ir("call-step-a"),
              .route_kind_hint = dasall::contracts::ToolIRRoute::LocalTool,
              .step_kind = dasall::tools::execution::WorkflowStepKind::Tool,
              .depends_on = {"step-b"},
              .allow_partial_side_effect = false,
              .timeout_override_ms = std::nullopt,
              .delegate_target = std::nullopt,
          },
          dasall::tools::execution::WorkflowStep{
              .step_id = "step-b",
              .tool_ir = make_ir("call-step-b"),
              .route_kind_hint = dasall::contracts::ToolIRRoute::LocalTool,
              .step_kind = dasall::tools::execution::WorkflowStepKind::Tool,
              .depends_on = {"step-a"},
              .allow_partial_side_effect = false,
              .timeout_override_ms = std::nullopt,
              .delegate_target = std::nullopt,
          },
      },
      .edges = {},
      .step_output_mapping = {},
      .delegation_policy = {},
      .metadata = {},
  };

  const dasall::tools::execution::WorkflowEngine engine;
  const auto batch_result = engine.build_batches(plan);
  assert_true(!batch_result.ok,
              "workflow engine should reject cyclic workflow graphs during batch construction");
  assert_equal(std::string("InvalidWorkflowPlan"), batch_result.failure_reason_code,
               "workflow engine should classify cyclic graphs as invalid workflow plans");

  const auto outcome = engine.execute(plan, make_ir("call-root"), execution_context);
  assert_equal(std::string("rejected"), outcome.receipt.status,
               "workflow engine should keep cyclic workflow plans in rejected status");
  assert_true(!outcome.tool_result.success.value_or(true),
              "workflow engine should surface cyclic rejection as a failed top-level ToolResult");
  assert_true(outcome.receipt.failure_digest.has_value() &&
                  outcome.receipt.failure_digest->details.message.find("workflow.cyclic_graph") !=
                      std::string::npos,
              "workflow engine should preserve cyclic rejection in the workflow-level failure digest");
  assert_true(outcome.tool_result.error.has_value() &&
                  outcome.tool_result.error->details.message.find("workflow.cyclic_graph") !=
                      std::string::npos,
              "workflow engine should project cyclic rejection into the top-level ToolResult error");
}

}  // namespace

int main() {
  try {
    test_workflow_engine_rejects_cyclic_graphs();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}