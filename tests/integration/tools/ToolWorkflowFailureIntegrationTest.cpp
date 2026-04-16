#include <algorithm>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "RuntimePolicySnapshot.h"
#include "ToolManager.h"
#include "error/ResultCode.h"
#include "execution/WorkflowEngine.h"
#include "registry/ToolRegistry.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::profiles::RuntimePolicySnapshot make_snapshot() {
  return dasall::profiles::RuntimePolicySnapshot{
      1U,
      "desktop_full",
      dasall::contracts::RuntimeBudget{
          .max_tokens = 4096U,
          .max_turns = 8U,
          .max_tool_calls = 24U,
          .max_latency_ms = 8000U,
          .max_replan_count = 2U,
      },
      dasall::profiles::ModelProfile{
          .stage_routes = {{
              "planner",
              dasall::profiles::ModelRoutePolicy{
                  .route = "local.small",
                  .fallback_route = std::string("builtin_only"),
                  .streaming_enabled = false,
              },
          }},
      },
      dasall::profiles::TokenBudgetPolicy{
          .max_input_tokens = 1024U,
          .max_output_tokens = 512U,
          .max_history_turns = 4U,
          .compression_threshold = 768U,
      },
      dasall::profiles::PromptPolicy{
          .allowed_prompt_releases = {"stable"},
          .trusted_sources = {"profiles"},
          .tool_visibility_rules = {"workflow:all"},
      },
      dasall::profiles::CapabilityCachePolicy{
          .refresh_interval_ms = 10000,
          .expire_after_ms = 180000,
          .stale_read_allowed = false,
          .failure_backoff_ms = 5000,
      },
      dasall::profiles::DegradePolicy{
          .fallback_chain = {"builtin_only"},
          .allow_model_failover = false,
          .allow_budget_degrade = true,
      },
      dasall::profiles::TimeoutPolicy{
          .llm = dasall::profiles::TimeoutBudget{
              .timeout_ms = 1800,
              .retry_budget = 0U,
              .circuit_breaker_threshold = 3U,
          },
          .tool = dasall::profiles::TimeoutBudget{
              .timeout_ms = 2500,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 4U,
          },
          .mcp = dasall::profiles::TimeoutBudget{
              .timeout_ms = 2000,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 3U,
          },
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
      dasall::profiles::OpsPolicy{
          .log_level = "info",
          .metrics_granularity = "full",
          .trace_sample_ratio = 0.5,
          .remote_diagnostics_enabled = false,
          .upgrade_strategy = "rolling",
      },
      4U};
}

[[nodiscard]] dasall::contracts::ToolDescriptor make_workflow_descriptor() {
  return dasall::contracts::ToolDescriptor{
      .tool_name = std::string("workflow.recovery"),
      .display_name = std::string("Workflow Recovery"),
      .category = dasall::contracts::ToolCategory::Workflow,
      .capability_tier = dasall::contracts::ToolCapabilityTier::Preview,
      .is_read_only = false,
      .supports_compensation = true,
      .default_timeout_ms = 5000U,
      .input_schema_ref = std::string("schema://tools/workflow.recovery/input/v1"),
      .output_schema_ref = std::string("schema://tools/workflow.recovery/output/v1"),
      .required_scopes = std::vector<std::string>{"tools.workflow"},
      .tags = std::vector<std::string>{"workflow", "integration"},
      .version = std::string("1.0.0"),
  };
}

[[nodiscard]] dasall::contracts::ToolRequest make_request() {
  return dasall::contracts::ToolRequest{
      .request_id = std::string("req-workflow-failure-int"),
      .tool_call_id = std::string("call-workflow-failure-int"),
      .tool_name = std::string("workflow.recovery"),
      .invocation_kind = dasall::contracts::ToolInvocationKind::Workflow,
      .arguments_payload = std::string("{}"),
      .created_at = 1000,
      .goal_id = std::string("goal-workflow-failure-int"),
      .worker_task_id = std::string("worker-workflow-failure-int"),
      .runtime_budget = std::nullopt,
      .timeout_ms = 5000U,
      .idempotency_key = std::string("idem-workflow-failure-int"),
      .tags = std::vector<std::string>{"integration", "workflow", "failure"},
  };
}

[[nodiscard]] dasall::contracts::ToolIR make_step_ir(
    std::string tool_name,
    std::string tool_call_id,
    std::string arguments_json) {
  return dasall::contracts::ToolIR{
      .request_id = std::string("req-workflow-failure-int"),
      .tool_call_id = std::move(tool_call_id),
      .tool_name = std::move(tool_name),
      .operation = dasall::contracts::ToolIROperation::Invoke,
      .normalized_arguments = std::move(arguments_json),
      .route = dasall::contracts::ToolIRRoute::LocalTool,
      .timeout_ms = 2000U,
      .idempotency_key = std::string("idem-workflow-failure-int"),
      .priority = dasall::contracts::ToolIRPriority::Normal,
      .goal_id = std::string("goal-workflow-failure-int"),
      .worker_task_id = std::string("worker-workflow-failure-int"),
  };
}

[[nodiscard]] dasall::tools::execution::WorkflowPlan make_plan() {
  return dasall::tools::execution::WorkflowPlan{
      .workflow_id = std::string("workflow.failure.integration"),
      .entry_step_ids = {"prepare"},
      .steps = {
          dasall::tools::execution::WorkflowStep{
              .step_id = "prepare",
              .tool_ir = make_step_ir(
                  "agent.prepare",
                  "call-prepare",
                  "{\"ticket\":\"pending\"}"),
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
      },
      .edges = {},
      .step_output_mapping = {},
      .delegation_policy = dasall::tools::execution::WorkflowDelegationPolicy{
          .mode = dasall::tools::execution::WorkflowDelegationMode::RecommendOnly,
          .max_delegate_steps = 1U,
      },
      .metadata = {{"template", "failure-integration-v1"}},
  };
}

[[nodiscard]] dasall::contracts::ToolResult make_prepare_result(
    const dasall::contracts::ToolIR& tool_ir) {
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

[[nodiscard]] dasall::contracts::ToolResult make_apply_failure_result(
    const dasall::contracts::ToolIR& tool_ir) {
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
              .stage = std::string("tests.integration.workflow.apply"),
          },
          .source_ref = dasall::contracts::ErrorSourceRefMinimal{
              .ref_type = std::string("workflow_integration_test"),
              .ref_id = std::string("apply"),
          },
      },
      .side_effects = std::vector<std::string>{"remote.commit"},
      .completed_at = 110,
      .duration_ms = 2,
      .goal_id = tool_ir.goal_id,
      .worker_task_id = tool_ir.worker_task_id,
      .tags = std::vector<std::string>{"workflow"},
  };
}

[[nodiscard]] dasall::tools::ToolManager make_manager() {
  auto registry = std::make_shared<dasall::tools::registry::ToolRegistry>();
  assert_true(registry->register_builtin(make_workflow_descriptor()),
              "workflow failure integration should register workflow descriptor before invoking ToolManager");

  auto workflow_engine = std::make_shared<dasall::tools::execution::WorkflowEngine>(
      dasall::tools::execution::WorkflowEngineDependencies{
          .plan_loader = [](const auto&, auto&, auto&) {
            return make_plan();
          },
          .builtin_executor = [](const auto& tool_ir, const auto&) {
            if (tool_ir.tool_name == std::optional<std::string>("agent.prepare")) {
              return make_prepare_result(tool_ir);
            }
            return make_apply_failure_result(tool_ir);
          },
          .mcp_executor = {},
      });

  dasall::tools::manager::ToolManagerDependencies dependencies;
  dependencies.registry = std::move(registry);
  dependencies.workflow_engine = std::move(workflow_engine);
  return dasall::tools::ToolManager(std::move(dependencies));
}

void test_tool_workflow_failure_integration_surfaces_failure_digest_sidecar_and_compensation_hints() {
  auto manager = make_manager();
  const auto snapshot = make_snapshot();
  const dasall::tools::ToolInvocationContext context{
      .caller_domain = std::string("runtime.main"),
      .session_id = std::string("session-workflow-failure-int"),
      .profile_snapshot = &snapshot,
      .trace = {
          .trace_id = std::string("trace-workflow-failure-int"),
          .span_id = std::string("span-workflow-failure-int"),
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::nullopt,
  };

  const auto envelope = manager.invoke(make_request(), context);

  assert_true(envelope.tool_result.has_value(),
              "workflow failure integration should always return ToolResult from ToolManager");
  assert_true(!envelope.tool_result->success.value_or(true),
              "workflow failure integration should fail top-level ToolResult when a workflow step fails");
  assert_true(envelope.tool_result->error.has_value() &&
                  envelope.tool_result->error->details.code.has_value() &&
                  *envelope.tool_result->error->details.code ==
                      static_cast<int>(dasall::contracts::ResultCode::ToolExecutionFailed),
              "workflow failure integration should expose workflow failure digest through top-level ToolResult.error");
  assert_true(envelope.failure_reason_code.has_value() &&
                  !envelope.failure_reason_code->empty() &&
                  envelope.tool_result->error.has_value() &&
                  envelope.tool_result->error->details.message == *envelope.failure_reason_code,
              "workflow failure integration should keep workflow failure reason on envelope.failure_reason_code");
  assert_true(envelope.tool_result->payload.has_value() &&
                  envelope.tool_result->payload->find("delegation_sidecar") != std::string::npos,
              "workflow failure integration should serialize delegation sidecar into workflow payload");
  assert_true(envelope.compensation_hints.has_value() &&
                  envelope.compensation_hints->size() == 1U,
              "workflow failure integration should surface workflow-scoped compensation_hints after failure");
  assert_true(envelope.compensation_hints->front().compensation_action == std::string("agent.prepare") &&
                  envelope.compensation_hints->front().target_ref == std::string("call-prepare"),
              "workflow failure integration should keep only reversible upstream effects in compensation hints");
  assert_true(envelope.compensation_hints->front().reason_code == std::string("workflow.compensation_available"),
              "workflow failure integration should expose compensation reason code for runtime recovery policy");
  assert_true(envelope.evidence_refs.has_value() && !envelope.evidence_refs->empty(),
              "workflow failure integration should keep workflow evidence refs for diagnostics");
  assert_true(envelope.route_facts.has_value() && envelope.route_facts->route_kind == std::string("workflow"),
              "workflow failure integration should preserve workflow route facts at ToolManager boundary");
  assert_true(envelope.observation.has_value() && envelope.observation_digest.has_value(),
              "workflow failure integration should still project failure ToolResult into Observation and ObservationDigest");

  const auto payload = *envelope.tool_result->payload;
  assert_true(payload.find("\"status\":\"failed\"") != std::string::npos,
              "workflow failure integration payload should mark failed status");
  assert_true(payload.find("\"failed_step_id\":\"apply\"") != std::string::npos,
              "workflow failure integration payload should expose failed step id");
  assert_true(payload.find("\"compensation_hint_count\":1") != std::string::npos,
              "workflow failure integration payload should include compensation hint count");
}

}  // namespace

int main() {
  try {
    test_tool_workflow_failure_integration_surfaces_failure_digest_sidecar_and_compensation_hints();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}