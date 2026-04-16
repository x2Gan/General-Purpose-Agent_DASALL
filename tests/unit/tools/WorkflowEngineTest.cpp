#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "RuntimePolicySnapshot.h"
#include "ToolManager.h"
#include "error/ResultCode.h"
#include "execution/WorkflowEngine.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::profiles::RuntimePolicySnapshot make_snapshot() {
  return dasall::profiles::RuntimePolicySnapshot{
      1U,
      "desktop_full",
      dasall::contracts::RuntimeBudget{
          .max_tokens = 2048U,
          .max_turns = 6U,
          .max_tool_calls = 12U,
          .max_latency_ms = 6000U,
          .max_replan_count = 1U,
      },
      dasall::profiles::ModelProfile{},
      dasall::profiles::TokenBudgetPolicy{},
      dasall::profiles::PromptPolicy{
          .allowed_prompt_releases = {"stable"},
          .trusted_sources = {"profiles"},
          .tool_visibility_rules = {"workflow:all"},
      },
      dasall::profiles::CapabilityCachePolicy{},
      dasall::profiles::DegradePolicy{},
      dasall::profiles::TimeoutPolicy{
          .llm = dasall::profiles::TimeoutBudget{},
          .tool = dasall::profiles::TimeoutBudget{},
          .mcp = dasall::profiles::TimeoutBudget{},
          .workflow = dasall::profiles::TimeoutBudget{
              .timeout_ms = 5000,
              .retry_budget = 0U,
              .circuit_breaker_threshold = 3U,
          },
      },
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
          .session_id = std::string("session-workflow-unit"),
          .profile_snapshot = &snapshot,
          .trace = {
              .trace_id = std::string("trace-workflow-unit"),
              .span_id = std::string("span-workflow-unit"),
              .parent_span_id = std::nullopt,
          },
          .confirmation_facts = std::nullopt,
      },
      .lane_key = std::string("workflow"),
  };
}

[[nodiscard]] dasall::contracts::ToolIR make_step_ir(
    std::string tool_name,
    std::string tool_call_id,
    std::string arguments_json) {
  return dasall::contracts::ToolIR{
      .request_id = std::string("req-workflow-unit"),
      .tool_call_id = std::move(tool_call_id),
      .tool_name = std::move(tool_name),
      .operation = dasall::contracts::ToolIROperation::Invoke,
      .normalized_arguments = std::move(arguments_json),
      .route = dasall::contracts::ToolIRRoute::LocalTool,
      .timeout_ms = 2000U,
      .idempotency_key = std::string("idem-workflow-unit"),
      .priority = dasall::contracts::ToolIRPriority::Normal,
      .goal_id = std::string("goal-workflow-unit"),
      .worker_task_id = std::string("worker-workflow-unit"),
  };
}

[[nodiscard]] dasall::contracts::ToolIR make_workflow_ir() {
  return dasall::contracts::ToolIR{
      .request_id = std::string("req-workflow-root"),
      .tool_call_id = std::string("call-workflow-root"),
      .tool_name = std::string("workflow.recovery"),
      .operation = dasall::contracts::ToolIROperation::Invoke,
      .normalized_arguments = std::string("{}"),
      .route = dasall::contracts::ToolIRRoute::WorkflowEngine,
      .timeout_ms = 5000U,
      .idempotency_key = std::string("idem-workflow-root"),
      .priority = dasall::contracts::ToolIRPriority::Normal,
      .goal_id = std::string("goal-workflow-root"),
      .worker_task_id = std::string("worker-workflow-root"),
  };
}

[[nodiscard]] dasall::tools::execution::WorkflowPlan make_plan() {
  return dasall::tools::execution::WorkflowPlan{
      .workflow_id = std::string("workflow.recovery.unit"),
      .entry_step_ids = {"prepare"},
      .steps = {
          dasall::tools::execution::WorkflowStep{
              .step_id = "prepare",
              .tool_ir = make_step_ir(
                  "agent.prepare",
                  "call-prepare",
                  "{\"seed\":\"alpha\"}"),
              .route_kind_hint = dasall::contracts::ToolIRRoute::LocalTool,
              .step_kind = dasall::tools::execution::WorkflowStepKind::Tool,
              .depends_on = {},
              .allow_partial_side_effect = false,
              .timeout_override_ms = std::nullopt,
              .delegate_target = std::nullopt,
          },
          dasall::tools::execution::WorkflowStep{
              .step_id = "delegate",
              .tool_ir = make_step_ir(
                  "agent.delegate",
                  "call-delegate",
                  "{}"),
              .route_kind_hint = dasall::contracts::ToolIRRoute::WorkflowEngine,
              .step_kind = dasall::tools::execution::WorkflowStepKind::Delegation,
              .depends_on = {"prepare"},
              .allow_partial_side_effect = false,
              .timeout_override_ms = std::nullopt,
              .delegate_target = std::string("multi_agent.review"),
          },
          dasall::tools::execution::WorkflowStep{
              .step_id = "apply",
              .tool_ir = make_step_ir(
                  "agent.apply",
                  "call-apply",
                  "{\"ticket\":\"pending\"}"),
              .route_kind_hint = dasall::contracts::ToolIRRoute::LocalTool,
              .step_kind = dasall::tools::execution::WorkflowStepKind::Tool,
              .depends_on = {"prepare", "delegate"},
              .allow_partial_side_effect = false,
              .timeout_override_ms = std::nullopt,
              .delegate_target = std::nullopt,
          },
          dasall::tools::execution::WorkflowStep{
              .step_id = "finalize",
              .tool_ir = make_step_ir(
                  "agent.finalize",
                  "call-finalize",
                  "{}"),
              .route_kind_hint = dasall::contracts::ToolIRRoute::LocalTool,
              .step_kind = dasall::tools::execution::WorkflowStepKind::Tool,
              .depends_on = {"apply"},
              .allow_partial_side_effect = false,
              .timeout_override_ms = std::nullopt,
              .delegate_target = std::nullopt,
          },
      },
      .edges = {},
      .step_output_mapping = {
          dasall::tools::execution::WorkflowStepOutputBinding{
              .source_step_id = "prepare",
              .source_json_pointer = "/ticket",
              .target_step_id = "apply",
              .target_argument_key = "ticket",
              .required = true,
          },
      },
      .delegation_policy = dasall::tools::execution::WorkflowDelegationPolicy{
          .mode = dasall::tools::execution::WorkflowDelegationMode::RecommendOnly,
          .max_delegate_steps = 1U,
      },
      .metadata = {{"template", "recovery-v1"}},
  };
}

void test_workflow_engine_builds_batches_and_stops_after_failure() {
  std::vector<std::string> executed_tools;
  const auto snapshot = make_snapshot();
  const auto execution_context = make_execution_context(snapshot);

  dasall::tools::execution::WorkflowEngine engine(
      dasall::tools::execution::WorkflowEngineDependencies{
          .plan_loader = {},
          .builtin_executor = [&executed_tools](const auto& tool_ir, const auto&) {
            executed_tools.push_back(tool_ir.tool_name.value_or(std::string("unknown")));
            if (*tool_ir.tool_name == "agent.prepare") {
              return dasall::contracts::ToolResult{
                  .request_id = tool_ir.request_id,
                  .tool_call_id = tool_ir.tool_call_id,
                  .tool_name = tool_ir.tool_name,
                  .success = true,
                  .payload = std::string("{\"ticket\":\"42\"}"),
                  .error = std::nullopt,
                                    .side_effects = std::vector<std::string>{"terminal.cwd_restore"},
                  .completed_at = 100,
                  .duration_ms = 1,
                  .goal_id = tool_ir.goal_id,
                  .worker_task_id = tool_ir.worker_task_id,
                  .tags = std::vector<std::string>{"workflow"},
              };
            }

            assert_true(tool_ir.normalized_arguments.has_value() &&
                            tool_ir.normalized_arguments->find("\"ticket\":\"42\"") !=
                                std::string::npos,
                        "workflow engine should inject mapped payload fields into downstream normalized arguments before dispatch");
            return dasall::contracts::ToolResult{
                .request_id = tool_ir.request_id,
                .tool_call_id = tool_ir.tool_call_id,
                .tool_name = tool_ir.tool_name,
                .success = false,
                .payload = std::string("{\"status\":\"failed\"}"),
                .error = dasall::contracts::ErrorInfo{
                    .failure_type = dasall::contracts::classify_result_code(
                        dasall::contracts::ResultCode::ToolExecutionFailed),
                    .retryable = false,
                    .safe_to_replan = true,
                    .details = dasall::contracts::ErrorDetails{
                        .code = static_cast<int>(dasall::contracts::ResultCode::ToolExecutionFailed),
                        .message = std::string("workflow.step_failed"),
                        .stage = std::string("tests.workflow.apply"),
                    },
                    .source_ref = dasall::contracts::ErrorSourceRefMinimal{
                        .ref_type = std::string("workflow_test"),
                        .ref_id = std::string("apply"),
                    },
                },
                .side_effects = std::nullopt,
                .completed_at = 110,
                .duration_ms = 2,
                .goal_id = tool_ir.goal_id,
                .worker_task_id = tool_ir.worker_task_id,
                .tags = std::vector<std::string>{"workflow"},
            };
          },
          .mcp_executor = {},
      });

  const auto plan = make_plan();
  const auto batch_result = engine.build_batches(plan);
  assert_true(batch_result.ok,
              "workflow engine should accept an acyclic workflow plan for batch construction");
  assert_equal(4, static_cast<int>(batch_result.batches.size()),
               "workflow engine should expand the dependency chain into four deterministic batches");
  assert_equal(std::string("prepare"), batch_result.batches[0].step_ids.front(),
               "workflow engine should place the entry step into the first batch");
  assert_equal(std::string("delegate"), batch_result.batches[1].step_ids.front(),
               "workflow engine should execute the delegation step after its dependency");
  assert_equal(std::string("apply"), batch_result.batches[2].step_ids.front(),
               "workflow engine should delay the apply step until both upstream dependencies are satisfied");

  const auto outcome = engine.execute(plan, make_workflow_ir(), execution_context);
  assert_equal(std::string("failed"), outcome.receipt.status,
               "workflow engine should stop the workflow when a step fails");
  assert_true(outcome.receipt.failed_step_id.has_value() &&
                  *outcome.receipt.failed_step_id == "apply",
              "workflow engine should expose the first failed step in the receipt");
  assert_equal(2, static_cast<int>(outcome.receipt.completed_step_ids.size()),
               "workflow engine should only mark successful tool and delegation steps as completed before the failure");
  assert_true(std::find(outcome.receipt.skipped_step_ids.begin(),
                        outcome.receipt.skipped_step_ids.end(),
                        std::string("finalize")) != outcome.receipt.skipped_step_ids.end(),
              "workflow engine should skip downstream steps after a failure stop");
  assert_true(outcome.receipt.delegation_sidecar.has_value() &&
                  outcome.receipt.delegation_sidecar->delegate_target == "multi_agent.review",
              "workflow engine should preserve delegation recommendation as a receipt sidecar");
    assert_true(outcome.receipt.compensation_hints.size() == 1U,
                            "workflow engine should aggregate reversible step side effects into workflow-scoped compensation hints");
    assert_true(outcome.compensation_hints.has_value() &&
                                    outcome.compensation_hints->size() == 1U &&
                                    outcome.compensation_hints->front().target_ref == std::string("call-prepare"),
                            "workflow engine should expose workflow compensation hints on the top-level execution outcome");
  assert_true(!outcome.tool_result.success.value_or(true),
              "workflow engine should project workflow failure into the top-level ToolResult");
  assert_true(outcome.tool_result.payload.has_value() &&
                  outcome.tool_result.payload->find("delegation_sidecar") != std::string::npos,
              "workflow engine should serialize delegation sidecar into the workflow ToolResult payload for runtime consumption");
  assert_equal(2, static_cast<int>(executed_tools.size()),
               "workflow engine should only dispatch builtin executors for non-delegation steps before failure stop");
  assert_equal(std::string("agent.prepare"), executed_tools[0],
               "workflow engine should execute the entry tool step first");
  assert_equal(std::string("agent.apply"), executed_tools[1],
               "workflow engine should dispatch the mapped failure step after delegation");
}

}  // namespace

int main() {
  try {
    test_workflow_engine_builds_batches_and_stops_after_failure();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}