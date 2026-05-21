#include <exception>
#include <iostream>
#include <string>

#include "RuntimePolicySnapshot.h"
#include "ToolManager.h"
#include "bridge/ToolServiceBridge.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::profiles::RuntimePolicySnapshot make_snapshot(
    bool stale_read_allowed,
    std::int64_t tool_timeout_ms) {
  return dasall::profiles::RuntimePolicySnapshot{
      1U,
      stale_read_allowed ? "edge_balanced" : "desktop_full",
      dasall::contracts::RuntimeBudget{
          .max_tokens = 2048U,
          .max_turns = 6U,
          .max_tool_calls = stale_read_allowed ? 6U : 18U,
          .max_latency_ms = 9000U,
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
          .refresh_interval_ms = 5000,
          .expire_after_ms = stale_read_allowed ? 60000 : 120000,
          .stale_read_allowed = stale_read_allowed,
          .failure_backoff_ms = 2000,
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
              .timeout_ms = tool_timeout_ms,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 4U,
          },
          .mcp = dasall::profiles::TimeoutBudget{
              .timeout_ms = 2200,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 3U,
          },
          .workflow = dasall::profiles::TimeoutBudget{
              .timeout_ms = 4800,
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

[[nodiscard]] dasall::contracts::ToolIR make_tool_ir(
    std::string request_id,
    std::string tool_call_id,
    std::string tool_name,
    std::string normalized_arguments,
    std::optional<std::uint32_t> timeout_ms,
    std::optional<std::string> goal_id = std::nullopt) {
  return dasall::contracts::ToolIR{
      .request_id = std::move(request_id),
      .tool_call_id = std::move(tool_call_id),
      .tool_name = std::move(tool_name),
      .operation = dasall::contracts::ToolIROperation::Invoke,
      .normalized_arguments = std::move(normalized_arguments),
      .route = dasall::contracts::ToolIRRoute::LocalTool,
      .timeout_ms = timeout_ms,
      .idempotency_key = std::string("idem-tool-service-bridge"),
      .priority = dasall::contracts::ToolIRPriority::Normal,
      .goal_id = std::move(goal_id),
      .worker_task_id = std::string("worker-tool-service-bridge"),
  };
}

void test_build_action_request_preserves_context_budget_and_payload() {
  const auto snapshot = make_snapshot(false, 2500);
  const dasall::tools::ToolInvocationContext invocation_context{
      .caller_domain = std::string("runtime.main"),
      .session_id = std::string("session-action"),
      .profile_snapshot = &snapshot,
      .trace = {
          .trace_id = std::string("trace-action"),
          .span_id = std::string("span-action"),
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::nullopt,
      .request_timeout_budget_ms = std::nullopt,
  };
  const auto tool_ir = make_tool_ir(
      "req-action",
      "call-action",
      "agent.terminal",
      "{\"command\":\"echo hi\"}",
      1800U,
      std::string("goal-action"));

  const dasall::tools::bridge::ToolServiceBridge bridge;
  const auto request = bridge.build_action_request(tool_ir, invocation_context);

  assert_equal(std::string("req-action"), request.context.request_id,
               "action request should preserve request_id");
  assert_equal(std::string("session-action"), request.context.session_id,
               "action request should preserve session_id");
  assert_equal(std::string("trace-action"), request.context.trace_id,
               "action request should preserve trace_id");
  assert_equal(std::string("call-action"), request.context.tool_call_id,
               "action request should preserve tool_call_id");
  assert_equal(std::string("goal-action"), request.context.goal_id,
               "action request should preserve goal_id");
  assert_true(request.context.budget_guard.has_value(),
              "action request should project RuntimeBudget from the profile snapshot");
  assert_equal(18, static_cast<int>(*request.context.budget_guard->max_tool_calls),
               "action request should preserve the profile tool-call budget");
  assert_equal(1800, static_cast<int>(request.context.deadline_ms),
               "action request should prefer ToolIR timeout_ms as deadline_ms");
  assert_equal(std::string("agent.terminal"), request.target.capability_id,
               "action request should map tool_name to capability_id");
  assert_equal(std::string("builtin:agent.terminal"), request.target.target_id,
               "action request should derive a stable builtin target id");
  assert_equal(std::string("agent.terminal"), request.action,
               "action request should preserve tool_name as the stable action key");
  assert_equal(std::string("{\"command\":\"echo hi\"}"), request.arguments_json,
               "action request should preserve normalized_arguments as JSON payload");
  assert_equal(std::string("idem-tool-service-bridge"),
               request.idempotency_key.value_or(""),
               "action request should preserve the idempotency key");
}

void test_build_query_request_uses_profile_freshness_and_fallback_correlation_ids() {
  const auto snapshot = make_snapshot(true, 1400);
  const dasall::tools::ToolInvocationContext invocation_context{
      .caller_domain = std::string("runtime.main"),
      .session_id = std::nullopt,
      .profile_snapshot = &snapshot,
      .trace = {
          .trace_id = std::nullopt,
          .span_id = std::nullopt,
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::nullopt,
      .request_timeout_budget_ms = std::nullopt,
  };
  const auto tool_ir = make_tool_ir(
      "req-query",
      "call-query",
      "memory.search",
      "{\"q\":\"status\"}",
      std::nullopt,
      std::nullopt);

  const dasall::tools::bridge::ToolServiceBridge bridge;
  const auto request = bridge.build_query_request(tool_ir, invocation_context);

  assert_equal(std::string("req-query.session"), request.context.session_id,
               "query request should synthesize a session id when runtime does not provide one");
  assert_equal(std::string("call-query.trace"), request.context.trace_id,
               "query request should synthesize a trace id when runtime does not provide one");
  assert_equal(std::string("call-query.goal"), request.context.goal_id,
               "query request should synthesize a goal id when ToolIR does not provide one");
  assert_equal(1400, static_cast<int>(request.context.deadline_ms),
               "query request should fall back to profile tool timeout when ToolIR timeout is absent");
  assert_equal(static_cast<int>(dasall::services::ServiceDataFreshness::allow_stale),
               static_cast<int>(request.freshness),
               "query request should align freshness with stale-read profile policy");
  assert_equal(std::string("memory.search"), request.dataset,
               "query request should preserve tool_name as dataset id");
  assert_equal(std::string("{\"q\":\"status\"}"), request.filters_json,
               "query request should preserve normalized_arguments as filters_json");
  assert_equal(std::string("default"), request.projection,
               "query request should use the default projection until descriptor-specific projection exists");
}

void test_build_diagnose_request_keeps_budget_optional_and_guards_minimum_deadline() {
  const dasall::tools::ToolInvocationContext invocation_context{
      .caller_domain = std::string("runtime.main"),
      .session_id = std::string("session-diagnose"),
      .profile_snapshot = nullptr,
      .trace = {
          .trace_id = std::string("trace-diagnose"),
          .span_id = std::nullopt,
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::nullopt,
      .request_timeout_budget_ms = std::nullopt,
  };
  auto tool_ir = make_tool_ir(
      "req-diagnose",
      "call-diagnose",
      "agent.terminal",
      "{\"include\":\"stderr\"}",
      std::nullopt,
      std::string("goal-diagnose"));
  tool_ir.idempotency_key = std::nullopt;

  const dasall::tools::bridge::ToolServiceBridge bridge;
  const auto request = bridge.build_diagnose_request(tool_ir, invocation_context);

  assert_true(!request.context.budget_guard.has_value(),
              "diagnose request should keep budget_guard empty when no profile snapshot is available");
  assert_equal(1, static_cast<int>(request.context.deadline_ms),
               "diagnose request should clamp deadline_ms to a positive minimum when no timeout source exists");
  assert_equal(std::string("agent.terminal"), request.target.capability_id,
               "diagnose request should preserve tool_name as capability id");
  assert_equal(std::string("builtin:agent.terminal"), request.target.target_id,
               "diagnose request should preserve the stable builtin target id");
  assert_true(request.include_last_error,
              "diagnose request should request last-error details by default");
}

void test_build_compensation_request_parses_structured_target_ref() {
  const auto snapshot = make_snapshot(false, 1800);
  const dasall::tools::ToolInvocationContext invocation_context{
      .caller_domain = std::string("runtime.main"),
      .session_id = std::string("session-compensate"),
      .profile_snapshot = &snapshot,
      .trace = {
          .trace_id = std::string("trace-compensate"),
          .span_id = std::string("span-compensate"),
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::nullopt,
      .request_timeout_budget_ms = std::nullopt,
  };
  const auto tool_ir = make_tool_ir(
      "req-compensate",
      "call-compensate",
      "agent.terminal",
      "{}",
      1700U,
      std::string("goal-compensate"));

  const dasall::tools::bridge::ToolServiceBridge bridge;
  const auto request = bridge.build_compensation_request(
      tool_ir,
      dasall::tools::CompensationRequest{
          .tool_call_id = std::string("call-compensate-source"),
          .compensation_action = std::string("safe_mode.exit"),
          .target_ref = std::string("tool://agent.terminal/call-compensate-source"),
          .reason_code = std::string("manual_recovery"),
          .evidence_refs = std::vector<std::string>{"evidence://compensate"},
      },
      invocation_context);

  assert_equal(std::string("req-compensate"), request.context.request_id,
               "compensation request should preserve request_id from ToolIR");
  assert_equal(std::string("agent.terminal"), request.target.capability_id,
               "compensation request should parse capability_id from tool:// target_ref");
  assert_equal(std::string("call-compensate-source"), request.target.target_id,
               "compensation request should parse target_id from tool:// target_ref");
  assert_equal(std::string("safe_mode.exit"), request.compensation_action,
               "compensation request should preserve compensation_action");
  assert_equal(std::string("call-compensate-source"), request.source_execution_id,
               "compensation request should reuse tool_call_id as source_execution_id handoff");
  assert_equal(std::string("manual_recovery"), request.reason_code,
               "compensation request should preserve reason_code");
}

void test_build_query_request_clamps_deadline_to_runtime_request_budget() {
  const auto snapshot = make_snapshot(false, 1400);
  const dasall::tools::ToolInvocationContext invocation_context{
      .caller_domain = std::string("runtime.main"),
      .session_id = std::string("session-budget-clamp"),
      .profile_snapshot = &snapshot,
      .trace = {
          .trace_id = std::string("trace-budget-clamp"),
          .span_id = std::nullopt,
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::nullopt,
      .request_timeout_budget_ms = 90U,
  };
  const auto tool_ir = make_tool_ir(
      "req-budget-clamp",
      "call-budget-clamp",
      "memory.search",
      "{\"q\":\"status\"}",
      std::nullopt,
      std::string("goal-budget-clamp"));

  const dasall::tools::bridge::ToolServiceBridge bridge;
  const auto request = bridge.build_query_request(tool_ir, invocation_context);

  assert_equal(90, static_cast<int>(request.context.deadline_ms),
               "query request should clamp deadline_ms to the runtime request timeout budget when present");
}

}  // namespace

int main() {
  try {
    test_build_action_request_preserves_context_budget_and_payload();
    test_build_query_request_uses_profile_freshness_and_fallback_correlation_ids();
        test_build_query_request_clamps_deadline_to_runtime_request_budget();
    test_build_diagnose_request_keeps_budget_optional_and_guards_minimum_deadline();
        test_build_compensation_request_parses_structured_target_ref();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}