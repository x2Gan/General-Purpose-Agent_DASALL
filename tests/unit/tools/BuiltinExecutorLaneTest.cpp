#include <algorithm>
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

class FakeExecutionService final : public dasall::services::IExecutionService {
 public:
  dasall::services::ExecutionCommandRequest last_execute_request;
  dasall::services::ExecutionDiagnoseRequest last_diagnose_request;
  int execute_calls = 0;
  int diagnose_calls = 0;

  std::function<dasall::services::ExecutionCommandResult(
      const dasall::services::ExecutionCommandRequest&)> execute_handler;
  std::function<dasall::services::ExecutionDiagnoseResult(
      const dasall::services::ExecutionDiagnoseRequest&)> diagnose_handler;

  dasall::services::ExecutionCommandResult execute(
      const dasall::services::ExecutionCommandRequest& request) override {
    ++execute_calls;
    last_execute_request = request;
    return execute_handler ? execute_handler(request) : dasall::services::ExecutionCommandResult{};
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
      const dasall::services::ExecutionDiagnoseRequest& request) override {
    ++diagnose_calls;
    last_diagnose_request = request;
    return diagnose_handler ? diagnose_handler(request)
                            : dasall::services::ExecutionDiagnoseResult{};
  }
};

class FakeDataService final : public dasall::services::IDataService {
 public:
  dasall::services::DataQueryRequest last_query_request;
  int query_calls = 0;

  std::function<dasall::services::DataQueryResult(
      const dasall::services::DataQueryRequest&)> query_handler;

  dasall::services::DataQueryResult query(
      const dasall::services::DataQueryRequest& request) override {
    ++query_calls;
    last_query_request = request;
    return query_handler ? query_handler(request) : dasall::services::DataQueryResult{};
  }

  dasall::services::DataCatalogResult list_capabilities(
      const dasall::services::DataCatalogRequest&) override {
    return dasall::services::DataCatalogResult{};
  }
};

[[nodiscard]] dasall::profiles::RuntimePolicySnapshot make_snapshot(bool stale_read_allowed) {
  return dasall::profiles::RuntimePolicySnapshot{
      1U,
      stale_read_allowed ? "edge_balanced" : "desktop_full",
      dasall::contracts::RuntimeBudget{
          .max_tokens = 2048U,
          .max_turns = 6U,
          .max_tool_calls = stale_read_allowed ? 4U : 16U,
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

[[nodiscard]] dasall::contracts::ToolDescriptor make_descriptor(
    std::string tool_name,
    dasall::contracts::ToolCategory category,
    bool is_read_only) {
  return dasall::contracts::ToolDescriptor{
      .tool_name = std::move(tool_name),
      .display_name = std::string("Builtin Lane Tool"),
      .category = category,
      .capability_tier = dasall::contracts::ToolCapabilityTier::Stable,
      .is_read_only = is_read_only,
      .supports_compensation = !is_read_only,
      .default_timeout_ms = 2200U,
      .input_schema_ref = std::string("schema://tools/builtin/input"),
      .output_schema_ref = std::string("schema://tools/builtin/output"),
      .required_scopes = std::vector<std::string>{"tools.execute"},
      .tags = std::vector<std::string>{"builtin"},
      .version = std::string("1.0.0"),
  };
}

[[nodiscard]] dasall::contracts::ToolIR make_tool_ir(
    std::string tool_name,
    std::string arguments_json,
    std::optional<std::string> goal_id = std::string("goal-builtin")) {
  return dasall::contracts::ToolIR{
      .request_id = std::string("req-builtin"),
      .tool_call_id = std::string("call-builtin"),
      .tool_name = std::move(tool_name),
      .operation = dasall::contracts::ToolIROperation::Invoke,
      .normalized_arguments = std::move(arguments_json),
      .route = dasall::contracts::ToolIRRoute::LocalTool,
      .timeout_ms = 1800U,
      .idempotency_key = std::string("idem-builtin"),
      .priority = dasall::contracts::ToolIRPriority::Normal,
      .goal_id = std::move(goal_id),
      .worker_task_id = std::string("worker-builtin"),
  };
}

[[nodiscard]] dasall::tools::ToolExecutionContext make_execution_context(
    const dasall::profiles::RuntimePolicySnapshot& snapshot) {
  return dasall::tools::ToolExecutionContext{
      .invocation_context = dasall::tools::ToolInvocationContext{
          .caller_domain = std::string("runtime.main"),
          .session_id = std::string("session-builtin"),
          .profile_snapshot = &snapshot,
          .trace = {
              .trace_id = std::string("trace-builtin"),
              .span_id = std::string("span-builtin"),
              .parent_span_id = std::nullopt,
          },
          .confirmation_facts = std::nullopt,
      },
      .lane_key = std::string("builtin"),
  };
}

void test_builtin_executor_lane_dispatches_action_and_preserves_side_effects() {
  const auto snapshot = make_snapshot(false);
  auto execution_service = std::make_shared<FakeExecutionService>();
  auto data_service = std::make_shared<FakeDataService>();
  execution_service->execute_handler = [](const auto&) {
    return dasall::services::ExecutionCommandResult{
                .code = std::nullopt,
        .execution_id = std::string("exec-builtin-action"),
        .payload_json = std::string("{\"stdout\":\"ok\"}"),
        .side_effects = {"terminal.executed"},
        .compensation_hints = {"terminal.cleanup"},
        .error = std::nullopt,
    };
  };

  dasall::tools::execution::BuiltinExecutorLane lane(
      dasall::tools::execution::BuiltinExecutorLaneDependencies{
          .registry = std::make_shared<dasall::tools::registry::ToolRegistry>(
              std::vector<dasall::contracts::ToolDescriptor>{
                  make_descriptor("agent.terminal", dasall::contracts::ToolCategory::Action, false),
              }),
          .service_bridge = std::make_shared<dasall::tools::bridge::ToolServiceBridge>(),
          .execution_service = execution_service,
          .data_service = data_service,
          .now_ms = [] { return 3000; },
      });

  const auto result = lane.execute(
      make_tool_ir("agent.terminal", "{\"command\":\"echo hi\"}"),
      make_execution_context(snapshot));

  assert_equal(1, execution_service->execute_calls,
               "action descriptor should dispatch through IExecutionService::execute");
  assert_equal(0, data_service->query_calls,
               "action descriptor should not dispatch through IDataService::query");
  assert_equal(std::string("agent.terminal"), execution_service->last_execute_request.action,
               "action dispatch should preserve tool_name as action key");
  assert_true(result.success.value_or(false),
              "action dispatch without service error should map to success");
  assert_equal(std::string("{\"stdout\":\"ok\"}"), result.payload.value_or(""),
               "action dispatch should preserve payload_json");
  assert_true(result.side_effects.has_value() && result.side_effects->size() == 1U,
              "action dispatch should preserve service side_effects");
}

void test_builtin_executor_lane_dispatches_query_and_marks_cache_hits() {
  const auto snapshot = make_snapshot(true);
  auto execution_service = std::make_shared<FakeExecutionService>();
  auto data_service = std::make_shared<FakeDataService>();
  data_service->query_handler = [](const auto&) {
    return dasall::services::DataQueryResult{
                .code = std::nullopt,
        .rows_json = std::string("[{\"status\":\"ready\"}]"),
        .from_cache = true,
        .error = std::nullopt,
    };
  };

  dasall::tools::execution::BuiltinExecutorLane lane(
      dasall::tools::execution::BuiltinExecutorLaneDependencies{
          .registry = std::make_shared<dasall::tools::registry::ToolRegistry>(
              std::vector<dasall::contracts::ToolDescriptor>{
                  make_descriptor("tool.inspect", dasall::contracts::ToolCategory::Information, true),
              }),
          .service_bridge = std::make_shared<dasall::tools::bridge::ToolServiceBridge>(),
          .execution_service = execution_service,
          .data_service = data_service,
          .now_ms = [] { return 3200; },
      });

  const auto result = lane.execute(
      make_tool_ir("tool.inspect", "{\"scope\":\"summary\"}"),
      make_execution_context(snapshot));

  assert_equal(0, execution_service->execute_calls,
               "information descriptor should not hit execution command dispatch");
  assert_equal(1, data_service->query_calls,
               "information descriptor should dispatch through IDataService::query");
  assert_equal(std::string("tool.inspect"), data_service->last_query_request.dataset,
               "query dispatch should preserve tool_name as dataset id");
  assert_equal(static_cast<int>(dasall::services::ServiceDataFreshness::allow_stale),
               static_cast<int>(data_service->last_query_request.freshness),
               "query dispatch should align freshness with profile stale-read policy");
  assert_true(result.success.value_or(false),
              "query dispatch without service error should map to success");
  assert_equal(std::string("[{\"status\":\"ready\"}]"), result.payload.value_or(""),
               "query dispatch should preserve rows_json as ToolResult payload");
  assert_true(result.tags.has_value() &&
                  std::find(result.tags->begin(), result.tags->end(), std::string("cache:hit")) !=
                      result.tags->end(),
              "query dispatch should tag cache hits for downstream observability");
}

void test_builtin_executor_lane_dispatches_diagnose_through_execution_service() {
  const auto snapshot = make_snapshot(false);
  auto execution_service = std::make_shared<FakeExecutionService>();
  auto data_service = std::make_shared<FakeDataService>();
  execution_service->diagnose_handler = [](const auto&) {
    return dasall::services::ExecutionDiagnoseResult{
                .code = std::nullopt,
        .target_reachable = true,
        .report_json = std::string("{\"health\":\"ok\"}"),
        .error = std::nullopt,
    };
  };

  dasall::tools::execution::BuiltinExecutorLane lane(
      dasall::tools::execution::BuiltinExecutorLaneDependencies{
          .registry = std::make_shared<dasall::tools::registry::ToolRegistry>(
              std::vector<dasall::contracts::ToolDescriptor>{
                  make_descriptor("tool.diagnose", dasall::contracts::ToolCategory::Diagnostic, true),
              }),
          .service_bridge = std::make_shared<dasall::tools::bridge::ToolServiceBridge>(),
          .execution_service = execution_service,
          .data_service = data_service,
          .now_ms = [] { return 3400; },
      });

  const auto result = lane.execute(
      make_tool_ir("tool.diagnose", "{\"include\":\"last_error\"}"),
      make_execution_context(snapshot));

  assert_equal(1, execution_service->diagnose_calls,
               "diagnostic descriptor should dispatch through IExecutionService::diagnose");
  assert_equal(0, data_service->query_calls,
               "diagnostic descriptor should not hit IDataService::query");
  assert_true(result.success.value_or(false),
              "diagnose dispatch without service error should map to success");
  assert_equal(std::string("{\"health\":\"ok\"}"), result.payload.value_or(""),
               "diagnose dispatch should preserve report_json as ToolResult payload");
}

void test_builtin_executor_lane_preserves_partial_side_effects_on_error() {
  const auto snapshot = make_snapshot(false);
  auto execution_service = std::make_shared<FakeExecutionService>();
  auto data_service = std::make_shared<FakeDataService>();
  execution_service->execute_handler = [](const auto&) {
    return dasall::services::ExecutionCommandResult{
        .code = dasall::contracts::ResultCode::ProviderTimeout,
        .execution_id = std::string("exec-builtin-partial"),
        .payload_json = std::string("{\"status\":\"partial\"}"),
        .side_effects = {"terminal.cwd_changed"},
        .compensation_hints = {"terminal.cwd_restore"},
        .error = dasall::contracts::ErrorInfo{
            .failure_type = dasall::contracts::ResultCodeCategory::Provider,
            .retryable = false,
            .safe_to_replan = false,
            .details = {
                .code = static_cast<int>(dasall::contracts::ResultCode::ProviderTimeout),
                .message = std::string("partial_side_effect"),
                .stage = std::string("service.execute"),
            },
            .source_ref = {
                .ref_type = std::string("adapter_receipt"),
                .ref_id = std::string("receipt-partial"),
            },
        },
    };
  };

  dasall::tools::execution::BuiltinExecutorLane lane(
      dasall::tools::execution::BuiltinExecutorLaneDependencies{
          .registry = std::make_shared<dasall::tools::registry::ToolRegistry>(
              std::vector<dasall::contracts::ToolDescriptor>{
                  make_descriptor("agent.terminal", dasall::contracts::ToolCategory::Action, false),
              }),
          .service_bridge = std::make_shared<dasall::tools::bridge::ToolServiceBridge>(),
          .execution_service = execution_service,
          .data_service = data_service,
          .now_ms = [] { return 3600; },
      });

  const auto result = lane.execute(
      make_tool_ir("agent.terminal", "{\"command\":\"cd /tmp\"}"),
      make_execution_context(snapshot));

  assert_true(!result.success.value_or(true),
              "service error should map to ToolResult failure");
  assert_true(result.error.has_value(),
              "service error should be preserved on ToolResult");
  assert_true(result.side_effects.has_value() && result.side_effects->size() == 1U,
              "partial side effects should remain visible for later compensation handling");
}

}  // namespace

int main() {
  try {
    test_builtin_executor_lane_dispatches_action_and_preserves_side_effects();
    test_builtin_executor_lane_dispatches_query_and_marks_cache_hits();
    test_builtin_executor_lane_dispatches_diagnose_through_execution_service();
    test_builtin_executor_lane_preserves_partial_side_effects_on_error();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}