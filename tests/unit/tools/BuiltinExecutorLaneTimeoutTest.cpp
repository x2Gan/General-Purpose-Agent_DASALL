#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "RuntimePolicySnapshot.h"
#include "execution/BuiltinExecutorLane.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class TimeoutExecutionService final : public dasall::services::IExecutionService {
 public:
  int execute_calls = 0;

  dasall::services::ExecutionCommandResult execute(
      const dasall::services::ExecutionCommandRequest&) override {
    ++execute_calls;
    return dasall::services::ExecutionCommandResult{
        .code = dasall::contracts::ResultCode::ProviderTimeout,
        .execution_id = std::string("exec-builtin-timeout"),
        .payload_json = std::string(),
        .side_effects = {},
        .compensation_hints = {},
        .error = dasall::contracts::ErrorInfo{
            .failure_type = dasall::contracts::ResultCodeCategory::Provider,
            .retryable = true,
            .safe_to_replan = false,
            .details = {
                .code = static_cast<int>(dasall::contracts::ResultCode::ProviderTimeout),
                .message = std::string("provider.timeout"),
                .stage = std::string("service.execute"),
            },
            .source_ref = {
                .ref_type = std::string("adapter_receipt"),
                .ref_id = std::string("receipt-timeout"),
            },
        },
    };
  }

  dasall::services::ExecutionCommandResult compensate(
      const dasall::services::ExecutionCompensationRequest&) override {
    return dasall::services::ExecutionCommandResult{};
  }

  dasall::services::ExecutionQueryResult query_state(
      const dasall::services::ExecutionQueryRequest&) override {
    return dasall::services::ExecutionQueryResult{};
  }

  dasall::services::ExecutionSubscriptionResult subscribe(
      const dasall::services::ExecutionSubscriptionRequest&) override {
    return dasall::services::ExecutionSubscriptionResult{};
  }

  dasall::services::ExecutionDiagnoseResult diagnose(
      const dasall::services::ExecutionDiagnoseRequest&) override {
    return dasall::services::ExecutionDiagnoseResult{};
  }
};

class NullDataService final : public dasall::services::IDataService {
 public:
  dasall::services::DataQueryResult query(
      const dasall::services::DataQueryRequest&) override {
    return dasall::services::DataQueryResult{};
  }

  dasall::services::DataCatalogResult list_capabilities(
      const dasall::services::DataCatalogRequest&) override {
    return dasall::services::DataCatalogResult{};
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

void test_builtin_executor_lane_surfaces_timeout_error_without_rewriting_it() {
  auto execution_service = std::make_shared<TimeoutExecutionService>();
  auto data_service = std::make_shared<NullDataService>();
  dasall::tools::execution::BuiltinExecutorLane lane(
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
          .data_service = data_service,
          .now_ms = [] { return 4200; },
      });

  const auto snapshot = make_snapshot();
  const auto result = lane.execute(
      dasall::contracts::ToolIR{
          .request_id = std::string("req-builtin-timeout"),
          .tool_call_id = std::string("call-builtin-timeout"),
          .tool_name = std::string("agent.terminal"),
          .operation = dasall::contracts::ToolIROperation::Invoke,
          .normalized_arguments = std::string("{\"command\":\"sleep 30\"}"),
          .route = dasall::contracts::ToolIRRoute::LocalTool,
          .timeout_ms = 1800U,
          .idempotency_key = std::string("idem-builtin-timeout"),
          .priority = dasall::contracts::ToolIRPriority::Normal,
          .goal_id = std::string("goal-builtin-timeout"),
          .worker_task_id = std::string("worker-builtin-timeout"),
      },
      dasall::tools::ToolExecutionContext{
          .invocation_context = dasall::tools::ToolInvocationContext{
              .caller_domain = std::string("runtime.main"),
              .session_id = std::string("session-builtin-timeout"),
              .profile_snapshot = &snapshot,
              .trace = {
                  .trace_id = std::string("trace-builtin-timeout"),
                  .span_id = std::string("span-builtin-timeout"),
                  .parent_span_id = std::nullopt,
              },
              .confirmation_facts = std::nullopt,
          },
          .lane_key = std::string("builtin"),
      });

  assert_equal(1, execution_service->execute_calls,
               "timeout path should still dispatch exactly once to execution service");
  assert_true(!result.success.value_or(true),
              "timeout error should map to ToolResult failure");
  assert_true(result.error.has_value(),
              "timeout error should remain structured on ToolResult");
  assert_equal(std::string("provider.timeout"), result.error->details.message,
               "timeout path should preserve service error message without rewriting it");
  assert_true(!result.side_effects.has_value(),
              "pure timeout path should not fabricate side_effects");
}

}  // namespace

int main() {
  try {
    test_builtin_executor_lane_surfaces_timeout_error_without_rewriting_it();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}