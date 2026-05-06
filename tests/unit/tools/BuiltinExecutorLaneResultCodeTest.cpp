#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "RuntimePolicySnapshot.h"
#include "execution/BuiltinExecutorLane.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class TriadExecutionService final : public dasall::services::IExecutionService {
 public:
  dasall::services::ExecutionCommandResult execute(
      const dasall::services::ExecutionCommandRequest&) override {
    return execute_result;
  }

  dasall::services::ExecutionCommandResult compensate(
      const dasall::services::ExecutionCompensationRequest&) override {
    return {};
  }

  dasall::services::ExecutionQueryResult query_state(
      const dasall::services::ExecutionQueryRequest&) override {
    return {};
  }

  dasall::services::ExecutionSubscriptionResult subscribe(
      const dasall::services::ExecutionSubscriptionRequest&) override {
    return {};
  }

  dasall::services::ExecutionDiagnoseResult diagnose(
      const dasall::services::ExecutionDiagnoseRequest&) override {
    return {};
  }

  dasall::services::ExecutionCommandResult execute_result;
};

class NullDataService final : public dasall::services::IDataService {
 public:
  dasall::services::DataQueryResult query(
      const dasall::services::DataQueryRequest&) override {
    return {};
  }

  dasall::services::DataCatalogResult list_capabilities(
      const dasall::services::DataCatalogRequest&) override {
    return {};
  }
};

[[nodiscard]] dasall::profiles::RuntimePolicySnapshot make_snapshot() {
  return dasall::profiles::RuntimePolicySnapshot{
      1U,
      "desktop_full",
      dasall::contracts::RuntimeBudget{
          .max_tokens = 2048U,
          .max_turns = 6U,
          .max_tool_calls = 12U,
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
          .expire_after_ms = 60000,
          .stale_read_allowed = false,
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
              .timeout_ms = 2200,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 4U,
          },
          .mcp = dasall::profiles::TimeoutBudget{
              .timeout_ms = 2000,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 3U,
          },
          .workflow = dasall::profiles::TimeoutBudget{
              .timeout_ms = 4000,
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

[[nodiscard]] dasall::contracts::ToolIR make_tool_ir() {
  return dasall::contracts::ToolIR{
      .request_id = std::string("req-builtin-result-code"),
      .tool_call_id = std::string("call-builtin-result-code"),
      .tool_name = std::string("agent.terminal"),
      .operation = dasall::contracts::ToolIROperation::Invoke,
      .normalized_arguments = std::string("{\"command\":\"echo ok\"}"),
      .route = dasall::contracts::ToolIRRoute::LocalTool,
      .timeout_ms = 1800U,
      .idempotency_key = std::string("idem-builtin-result-code"),
      .priority = dasall::contracts::ToolIRPriority::Normal,
      .goal_id = std::string("goal-builtin-result-code"),
      .worker_task_id = std::string("worker-builtin-result-code"),
  };
}

[[nodiscard]] dasall::tools::ToolExecutionContext make_execution_context(
    const dasall::profiles::RuntimePolicySnapshot& snapshot) {
  return dasall::tools::ToolExecutionContext{
      .invocation_context = dasall::tools::ToolInvocationContext{
          .caller_domain = std::string("runtime.main"),
          .session_id = std::string("session-builtin-result-code"),
          .profile_snapshot = &snapshot,
          .trace = {
              .trace_id = std::string("trace-builtin-result-code"),
              .span_id = std::string("span-builtin-result-code"),
              .parent_span_id = std::nullopt,
          },
          .confirmation_facts = std::nullopt,
      },
      .lane_key = std::string("builtin"),
  };
}

[[nodiscard]] dasall::tools::execution::BuiltinExecutorLane make_lane(
    const std::shared_ptr<TriadExecutionService>& execution_service) {
  return dasall::tools::execution::BuiltinExecutorLane(
      dasall::tools::execution::BuiltinExecutorLaneDependencies{
          .registry = std::make_shared<dasall::tools::registry::ToolRegistry>(
              std::vector<dasall::contracts::ToolDescriptor>{
                  dasall::contracts::ToolDescriptor{
                      .tool_name = std::string("agent.terminal"),
                      .display_name = std::string("Agent Terminal"),
                      .category = dasall::contracts::ToolCategory::Action,
                      .capability_tier = dasall::contracts::ToolCapabilityTier::Stable,
                      .is_read_only = false,
                      .supports_compensation = false,
                      .default_timeout_ms = 2200U,
                      .input_schema_ref = std::string("schema://tools/agent.terminal/input/v1"),
                      .output_schema_ref = std::string("schema://tools/agent.terminal/output/v1"),
                      .required_scopes = std::vector<std::string>{"tools.execute"},
                      .tags = std::vector<std::string>{"builtin"},
                      .version = std::string("1.0.0"),
                  },
              }),
          .service_bridge = std::make_shared<dasall::tools::bridge::ToolServiceBridge>(),
          .execution_service = execution_service,
          .data_service = std::make_shared<NullDataService>(),
          .now_ms = [] { return 5000; },
      });
}

void test_builtin_executor_lane_accepts_success_without_synthetic_failure_code() {
  const auto snapshot = make_snapshot();
  auto execution_service = std::make_shared<TriadExecutionService>();
  execution_service->execute_result = dasall::services::ExecutionCommandResult{
      .code = std::nullopt,
      .execution_id = std::string("exec-builtin-result-code"),
      .payload_json = std::string("{\"stdout\":\"ok\"}"),
      .side_effects = {"terminal.executed"},
      .compensation_hints = {},
      .error = std::nullopt,
  };
  auto lane = make_lane(execution_service);

  const auto result = lane.execute(make_tool_ir(), make_execution_context(snapshot));

  assert_true(result.success.value_or(false),
              "builtin executor lane should accept success payloads without forcing a failure code");
  assert_true(!result.error.has_value(),
              "builtin executor lane should keep the success path free of synthesized errors");
  assert_equal(std::string("{\"stdout\":\"ok\"}"), result.payload.value_or(""),
               "builtin executor lane should preserve the success payload on consistent service triads");
}

void test_builtin_executor_lane_rejects_failure_code_without_error() {
  const auto snapshot = make_snapshot();
  auto execution_service = std::make_shared<TriadExecutionService>();
  execution_service->execute_result = dasall::services::ExecutionCommandResult{
      .code = dasall::contracts::ResultCode::ToolExecutionFailed,
      .execution_id = std::string("exec-builtin-inconsistent"),
      .payload_json = std::string("{\"stdout\":\"ok\"}"),
      .side_effects = {},
      .compensation_hints = {},
      .error = std::nullopt,
  };
  auto lane = make_lane(execution_service);

  const auto result = lane.execute(make_tool_ir(), make_execution_context(snapshot));

  assert_true(!result.success.value_or(true),
              "builtin executor lane should reject service payloads that still carry a failure code without error details");
  assert_true(result.error.has_value(),
              "builtin executor lane should synthesize a structured error for inconsistent service result triads");
  assert_equal(std::string("builtin.executor.service_result_inconsistent"),
               result.error->details.message,
               "builtin executor lane should surface an explicit triad inconsistency message");
  assert_true(!result.payload.has_value(),
              "builtin executor lane should drop the payload when the upstream service triad is inconsistent");
}

}  // namespace

int main() {
  try {
    test_builtin_executor_lane_accepts_success_without_synthetic_failure_code();
    test_builtin_executor_lane_rejects_failure_code_without_error();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
