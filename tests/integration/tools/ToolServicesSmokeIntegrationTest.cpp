#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "RuntimePolicySnapshot.h"
#include "ToolManager.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::contracts::ToolRequest make_request() {
  return dasall::contracts::ToolRequest{
      .request_id = std::string("req-tool-smoke"),
      .tool_call_id = std::string("call-tool-smoke"),
      .tool_name = std::string("agent.terminal"),
      .invocation_kind = dasall::contracts::ToolInvocationKind::Action,
      .arguments_payload = std::string("{\"command\":\"echo smoke\"}"),
      .created_at = 1000,
      .goal_id = std::string("goal-tool-smoke"),
      .worker_task_id = std::string("worker-tool-smoke"),
      .runtime_budget = std::nullopt,
      .timeout_ms = 2500U,
      .idempotency_key = std::string("idem-tool-smoke"),
      .tags = std::vector<std::string>{"integration", "tools"},
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

void test_tool_services_smoke_enters_top_level_integration_graph() {
  dasall::tools::ToolManager manager;
  const auto snapshot = make_snapshot();
  const dasall::tools::ToolInvocationContext context{
      .caller_domain = std::string("runtime.main"),
      .session_id = std::string("session-tool-smoke"),
      .profile_snapshot = &snapshot,
      .trace = {
          .trace_id = std::string("trace-tool-smoke"),
          .span_id = std::string("span-tool-smoke"),
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::vector<dasall::tools::ToolConfirmationFact>{
          dasall::tools::ToolConfirmationFact{
              .confirmation_id = std::string("confirm-tool-smoke"),
              .subject_ref = std::string("goal://tool-smoke"),
              .proof_type = std::string("user.approved"),
              .confirmed_at_ms = 900,
          }},
  };

  const auto envelope = manager.invoke(make_request(), context);

  assert_true(envelope.tool_result.has_value(),
              "tools integration smoke should produce a ToolResult");
  assert_true(envelope.tool_result->success.value_or(false),
              "tools integration smoke should complete successfully on builtin route");
  assert_true(envelope.has_projection(),
              "tools integration smoke should emit observation and digest together");
  assert_true(envelope.route_facts.has_value(),
              "tools integration smoke should expose route facts");
  assert_equal(std::string("builtin"), *envelope.route_facts->route_kind,
               "tools integration smoke should use builtin route by default");
  assert_true(!envelope.failure_reason_code.has_value(),
              "tools integration smoke should not produce failure reason on success");
}

}  // namespace

int main() {
  try {
    test_tool_services_smoke_enters_top_level_integration_graph();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}