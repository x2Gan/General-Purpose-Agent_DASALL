#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "RuntimePolicySnapshot.h"
#include "ToolManager.h"
#include "observation/ObservationSource.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::contracts::ToolRequest make_request() {
  return dasall::contracts::ToolRequest{
      .request_id = std::string("req-pipeline"),
      .tool_call_id = std::string("call-pipeline"),
      .tool_name = std::string("agent.terminal"),
      .invocation_kind = dasall::contracts::ToolInvocationKind::Action,
      .arguments_payload = std::string("{}"),
      .created_at = 1000,
      .goal_id = std::string("goal-pipeline"),
      .worker_task_id = std::string("worker-pipeline"),
      .runtime_budget = std::nullopt,
      .timeout_ms = 2500U,
      .idempotency_key = std::string("idem-pipeline"),
      .tags = std::vector<std::string>{"builtin"},
  };
}

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
          .tool_visibility_rules = {"builtin:all"},
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
              .retry_budget = 1U,
              .circuit_breaker_threshold = 3U,
          },
      },
      dasall::profiles::ExecutionPolicy{
          .requires_high_risk_confirmation = true,
          .safe_mode_enabled = true,
          .audit_level = "full",
          .allowed_tool_domains = {"builtin"},
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

void test_pipeline_connects_registry_validation_policy_route_execution_and_projection() {
  int requested_count = 0;
  int completed_count = 0;
  int failed_count = 0;

  dasall::tools::manager::ToolManagerDependencies dependencies;
  dependencies.executor = [](const dasall::tools::manager::ToolExecutionRequest& execution_request) {
    assert_equal(std::string("builtin"), execution_request.route_decision.lane_key,
                 "pipeline should route builtin terminal calls to the builtin lane");
    return dasall::contracts::ToolResult{
        .request_id = execution_request.tool_ir.request_id,
        .tool_call_id = execution_request.tool_ir.tool_call_id,
        .tool_name = execution_request.tool_ir.tool_name,
        .success = true,
        .payload = std::string("{\"stdout\":\"ok\"}"),
        .error = std::nullopt,
        .side_effects = std::vector<std::string>{"side_effect:none"},
        .completed_at = 2000,
        .duration_ms = 12,
        .goal_id = execution_request.tool_ir.goal_id,
        .worker_task_id = execution_request.tool_ir.worker_task_id,
        .tags = std::vector<std::string>{"pipeline"},
    };
  };
  dependencies.audit_hooks.on_requested = [&requested_count](const auto&, const auto&) {
    ++requested_count;
  };
  dependencies.audit_hooks.on_completed = [&completed_count](const auto& envelope) {
    ++completed_count;
    assert_true(envelope.has_projection(),
                "completed audit hook should observe the projected observation surface");
  };
  dependencies.audit_hooks.on_failed = [&failed_count](const auto&) {
    ++failed_count;
  };

  dasall::tools::ToolManager manager(std::move(dependencies));
  const auto snapshot = make_snapshot();
  const dasall::tools::ToolInvocationContext context{
      .caller_domain = std::string("runtime.main"),
      .session_id = std::string("session-pipeline"),
      .profile_snapshot = &snapshot,
      .trace = {
          .trace_id = std::string("trace-pipeline"),
          .span_id = std::string("span-pipeline"),
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::vector<dasall::tools::ToolConfirmationFact>{
          dasall::tools::ToolConfirmationFact{
              .confirmation_id = std::string("confirm-1"),
              .subject_ref = std::string("goal://pipeline"),
              .proof_type = std::string("user.approved"),
              .confirmed_at_ms = 900,
          }},
  };

  const auto envelope = manager.invoke(make_request(), context);
  assert_true(envelope.tool_result.has_value(),
              "successful pipeline invocation should return a tool_result");
  assert_true(envelope.tool_result->success.value_or(false),
              "successful pipeline invocation should mark the result successful");
  assert_true(envelope.has_projection(),
              "successful pipeline invocation should include observation and digest");
  assert_true(!envelope.failure_reason_code.has_value(),
              "successful pipeline invocation should not carry a failure reason");
  assert_true(envelope.route_facts.has_value(),
              "successful pipeline invocation should expose route facts");
  assert_equal(std::string("builtin"), *envelope.route_facts->route_kind,
               "route facts should expose the selected builtin lane kind");
  assert_equal(std::string("route.builtin.selected"),
               *envelope.route_facts->decision_reason,
               "route facts should preserve the selector reason code");
  assert_true(envelope.observation.has_value(),
              "successful pipeline invocation should emit an observation");
  assert_equal(static_cast<int>(dasall::contracts::ObservationSource::ToolExecution),
               static_cast<int>(*envelope.observation->source),
               "observation source should remain ToolExecution");
  assert_true(envelope.observation_digest.has_value(),
              "successful pipeline invocation should emit an observation digest");
  assert_true(envelope.observation_digest->summary.has_value() &&
                  !envelope.observation_digest->summary->empty(),
              "observation digest should include a non-empty summary");
  assert_true(envelope.evidence_refs.has_value() && !envelope.evidence_refs->empty(),
              "successful pipeline invocation should preserve evidence references");
  assert_true(envelope.compensation_hints.has_value() &&
                  envelope.compensation_hints->size() == 1U &&
                  envelope.compensation_hints->front().compensation_action ==
                      std::string("agent.terminal") &&
                  envelope.compensation_hints->front().target_ref ==
                      std::string("tool://agent.terminal/call-pipeline"),
              "compensatable builtin descriptors should surface parseable compensation hints when side effects exist");
  assert_equal(1, requested_count,
               "audit hooks should see the request exactly once");
  assert_equal(1, completed_count,
               "audit hooks should see the completed envelope exactly once");
  assert_equal(0, failed_count,
               "successful pipeline invocation should not trigger failed audit hooks");
}

}  // namespace

int main() {
  try {
    test_pipeline_connects_registry_validation_policy_route_execution_and_projection();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}