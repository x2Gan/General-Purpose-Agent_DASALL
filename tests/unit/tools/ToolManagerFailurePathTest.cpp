#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "RuntimePolicySnapshot.h"
#include "ToolManager.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::profiles::RuntimePolicySnapshot make_snapshot(
    std::vector<std::string> allowed_domains,
    bool requires_confirmation) {
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
          .requires_high_risk_confirmation = requires_confirmation,
          .safe_mode_enabled = true,
          .audit_level = "full",
          .allowed_tool_domains = std::move(allowed_domains),
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

[[nodiscard]] dasall::contracts::ToolDescriptor make_descriptor(
    std::string tool_name,
    dasall::contracts::ToolCategory category,
    bool is_read_only) {
  return dasall::contracts::ToolDescriptor{
      .tool_name = std::move(tool_name),
      .display_name = std::string("Failure Tool"),
      .category = category,
      .capability_tier = dasall::contracts::ToolCapabilityTier::Stable,
      .is_read_only = is_read_only,
      .supports_compensation = false,
      .default_timeout_ms = 2500U,
      .input_schema_ref = std::string("schema://tools/failure/input"),
      .output_schema_ref = std::string("schema://tools/failure/output"),
      .required_scopes = std::vector<std::string>{"tools.execute"},
      .tags = std::vector<std::string>{"builtin"},
      .version = std::string("1.0.0"),
  };
}

[[nodiscard]] dasall::contracts::ToolRequest make_request(
    std::string tool_name,
    dasall::contracts::ToolInvocationKind invocation_kind) {
  return dasall::contracts::ToolRequest{
      .request_id = std::string("req-failure-1"),
      .tool_call_id = std::string("call-failure-1"),
      .tool_name = std::move(tool_name),
      .invocation_kind = invocation_kind,
      .arguments_payload = std::string("{}"),
      .created_at = 1000,
      .goal_id = std::string("goal-failure"),
      .worker_task_id = std::string("worker-failure"),
      .runtime_budget = std::nullopt,
      .timeout_ms = 2500U,
      .idempotency_key = std::string("idem-failure"),
      .tags = std::vector<std::string>{"builtin"},
  };
}

void test_missing_descriptor_and_policy_denial_fail_closed() {
  dasall::tools::manager::ToolManagerDependencies dependencies;
  dependencies.registry = std::make_shared<dasall::tools::registry::ToolRegistry>(
      std::vector<dasall::contracts::ToolDescriptor>{
          make_descriptor("tool.apply", dasall::contracts::ToolCategory::Action, false),
          make_descriptor("tool.inspect", dasall::contracts::ToolCategory::Information, true),
      });

  dasall::tools::ToolManager manager(std::move(dependencies));
  const auto snapshot = make_snapshot({"builtin"}, true);
  const dasall::tools::ToolInvocationContext context{
      .caller_domain = std::string("runtime.main"),
      .session_id = std::string("session-failure"),
      .profile_snapshot = &snapshot,
      .trace = {
          .trace_id = std::string("trace-failure"),
          .span_id = std::string("span-failure"),
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::nullopt,
  };

  const auto missing_descriptor = manager.invoke(
      make_request("tool.missing", dasall::contracts::ToolInvocationKind::Action),
      context);
  assert_true(missing_descriptor.tool_result.has_value(),
              "missing descriptor path should still produce a unified envelope");
  assert_equal(std::string("tool.manager.descriptor_missing"),
               *missing_descriptor.failure_reason_code,
               "missing descriptor should fail closed with a descriptor-missing reason");
  assert_true(!missing_descriptor.has_projection(),
              "descriptor resolution failure should not fabricate a projection");

  const auto confirmation_denied = manager.invoke(
      make_request("tool.apply", dasall::contracts::ToolInvocationKind::Action),
      context);
  assert_true(confirmation_denied.tool_result.has_value(),
              "policy denial should still produce a unified envelope");
  assert_equal(std::string("policy.confirmation_required"),
               *confirmation_denied.failure_reason_code,
               "high-risk request without confirmation should be denied by policy gate");
  assert_true(!confirmation_denied.has_projection(),
              "policy denial should not fabricate a projection");
}

void test_route_unavailable_is_exposed_as_fail_closed_reason() {
  int execution_count = 0;
  dasall::tools::manager::ToolManagerDependencies dependencies;
  dependencies.registry = std::make_shared<dasall::tools::registry::ToolRegistry>(
      std::vector<dasall::contracts::ToolDescriptor>{
          make_descriptor("tool.inspect", dasall::contracts::ToolCategory::Information, true),
      });
  dependencies.executor = [&execution_count](const auto&) {
    ++execution_count;
    return dasall::contracts::ToolResult{};
  };

  dasall::tools::ToolManager manager(std::move(dependencies));
  manager.set_route_health(dasall::tools::route::ToolRouteHealthSnapshot{
      .builtin_lane_healthy = false,
      .workflow_lane_healthy = true,
      .mcp_lane_healthy = false,
  });

  const auto snapshot = make_snapshot({"builtin"}, false);
  const dasall::tools::ToolInvocationContext context{
      .caller_domain = std::string("runtime.main"),
      .session_id = std::string("session-route"),
      .profile_snapshot = &snapshot,
      .trace = {
          .trace_id = std::string("trace-route"),
          .span_id = std::string("span-route"),
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::nullopt,
  };

  const auto route_unavailable = manager.invoke(
      make_request("tool.inspect", dasall::contracts::ToolInvocationKind::InformationQuery),
      context);
  assert_equal(std::string("RouteUnavailable"), *route_unavailable.failure_reason_code,
               "route selector unavailability should be surfaced directly");
  assert_true(route_unavailable.route_facts.has_value(),
              "route failure should still expose route facts");
  assert_equal(std::string("RouteUnavailable"),
               *route_unavailable.route_facts->decision_reason,
               "route facts should preserve the route selector reason");
  assert_equal(0, execution_count,
               "route-unavailable requests must fail before hitting the executor");
}

}  // namespace

int main() {
  try {
    test_missing_descriptor_and_policy_denial_fail_closed();
    test_route_unavailable_is_exposed_as_fail_closed_reason();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}