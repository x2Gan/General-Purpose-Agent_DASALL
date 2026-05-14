#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "RuntimePolicySnapshot.h"
#include "ServiceLiveComposition.h"
#include "ToolManager.h"
#include "bridge/ToolServiceBridge.h"
#include "execution/BuiltinExecutorLane.h"
#include "registry/ToolRegistry.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_true;

[[nodiscard]] bool contains_string(const std::vector<std::string>& values,
                                   const std::string& expected) {
  return std::find(values.begin(), values.end(), expected) != values.end();
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

[[nodiscard]] dasall::contracts::ToolDescriptor make_action_descriptor() {
  return dasall::contracts::ToolDescriptor{
      .tool_name = std::string("agent.terminal"),
      .display_name = std::string("Agent Terminal"),
      .category = dasall::contracts::ToolCategory::Action,
      .capability_tier = dasall::contracts::ToolCapabilityTier::Preview,
      .is_read_only = false,
      .supports_compensation = false,
      .default_timeout_ms = 30000U,
      .input_schema_ref = std::string("schema://tools/agent.terminal/input/v1"),
      .output_schema_ref = std::string("schema://tools/agent.terminal/output/v1"),
      .required_scopes = std::vector<std::string>{"tools.execute"},
      .tags = std::vector<std::string>{"builtin", "action"},
      .version = std::string("1.0.0"),
  };
}

[[nodiscard]] dasall::contracts::ToolDescriptor make_query_descriptor() {
  return dasall::contracts::ToolDescriptor{
      .tool_name = std::string("agent.dataset"),
      .display_name = std::string("Agent Dataset"),
      .category = dasall::contracts::ToolCategory::Information,
      .capability_tier = dasall::contracts::ToolCapabilityTier::Preview,
      .is_read_only = true,
      .supports_compensation = false,
      .default_timeout_ms = 30000U,
      .input_schema_ref = std::string("schema://tools/agent.dataset/input/v1"),
      .output_schema_ref = std::string("schema://tools/agent.dataset/output/v1"),
      .required_scopes = std::vector<std::string>{"tools.read"},
      .tags = std::vector<std::string>{"builtin", "query"},
      .version = std::string("1.0.0"),
  };
}

[[nodiscard]] dasall::contracts::ToolRequest make_action_request() {
  return dasall::contracts::ToolRequest{
      .request_id = std::string("req-tool-prod-action"),
      .tool_call_id = std::string("call-tool-prod-action"),
      .tool_name = std::string("agent.terminal"),
      .invocation_kind = dasall::contracts::ToolInvocationKind::Action,
      .arguments_payload = std::string("{\"command\":\"echo production\"}"),
      .created_at = 2000,
      .goal_id = std::string("goal-tool-prod-action"),
      .worker_task_id = std::string("worker-tool-prod-action"),
      .runtime_budget = std::nullopt,
      .timeout_ms = 2500U,
      .idempotency_key = std::string("idem-tool-prod-action"),
      .tags = std::vector<std::string>{"integration", "tools", "action"},
  };
}

[[nodiscard]] dasall::contracts::ToolRequest make_query_request() {
  return dasall::contracts::ToolRequest{
      .request_id = std::string("req-tool-prod-query"),
      .tool_call_id = std::string("call-tool-prod-query"),
      .tool_name = std::string("agent.dataset"),
      .invocation_kind = dasall::contracts::ToolInvocationKind::InformationQuery,
      .arguments_payload = std::string("{\"scope\":\"production\"}"),
      .created_at = 2001,
      .goal_id = std::string("goal-tool-prod-query"),
      .worker_task_id = std::string("worker-tool-prod-query"),
      .runtime_budget = std::nullopt,
      .timeout_ms = 2500U,
      .idempotency_key = std::string("idem-tool-prod-query"),
      .tags = std::vector<std::string>{"integration", "tools", "query"},
  };
}

[[nodiscard]] dasall::tools::ToolManager make_production_bridge_manager() {
  const auto snapshot = make_snapshot();
  const auto live_services = dasall::services::compose_live_services(snapshot);
  assert_true(live_services.ok(),
              std::string("production bridge integration should compose live services: ") +
                  live_services.error);

  auto registry = std::make_shared<dasall::tools::registry::ToolRegistry>();
  assert_true(registry->register_builtin(make_action_descriptor()),
              "production bridge integration should register the builtin action descriptor");
  assert_true(registry->register_builtin(make_query_descriptor()),
              "production bridge integration should register the builtin query descriptor");

  auto builtin_lane = std::make_shared<dasall::tools::execution::BuiltinExecutorLane>(
      dasall::tools::execution::BuiltinExecutorLaneDependencies{
          .registry = registry,
          .service_bridge = std::make_shared<dasall::tools::bridge::ToolServiceBridge>(),
          .execution_service = live_services.execution_service,
          .data_service = live_services.data_service,
          .now_ms = {},
      });

  dasall::tools::manager::ToolManagerDependencies dependencies;
  dependencies.registry = registry;
  dependencies.executor = [builtin_lane](const auto& execution_request) {
    return builtin_lane->execute(
        execution_request.tool_ir,
        dasall::tools::ToolExecutionContext{
            .invocation_context = execution_request.invocation_context,
            .lane_key = execution_request.route_decision.lane_key,
        });
  };
  return dasall::tools::ToolManager(std::move(dependencies));
}

[[nodiscard]] dasall::tools::ToolInvocationContext make_context(
    const dasall::profiles::RuntimePolicySnapshot& snapshot) {
  return dasall::tools::ToolInvocationContext{
      .caller_domain = std::string("runtime.main"),
      .session_id = std::string("session-tool-prod"),
      .profile_snapshot = &snapshot,
      .trace = {
          .trace_id = std::string("trace-tool-prod"),
          .span_id = std::string("span-tool-prod"),
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::vector<dasall::tools::ToolConfirmationFact>{
          dasall::tools::ToolConfirmationFact{
              .confirmation_id = std::string("confirm-tool-prod"),
              .subject_ref = std::string("goal://tool-prod"),
              .proof_type = std::string("user.approved"),
              .confirmed_at_ms = 1900,
          }},
  };
}

void test_tool_services_production_bridge_uses_live_services_backend() {
  auto manager = make_production_bridge_manager();
  const auto snapshot = make_snapshot();
  const auto context = make_context(snapshot);

  const auto action_envelope = manager.invoke(make_action_request(), context);
  const auto query_envelope = manager.invoke(make_query_request(), context);

  assert_true(action_envelope.tool_result.has_value() &&
                  action_envelope.tool_result->success.value_or(false),
              "production bridge integration should keep the action path successful");
  assert_true(action_envelope.tool_result->payload.has_value() &&
                  action_envelope.tool_result->payload->find("\"operation\":\"agent.terminal\"") != std::string::npos,
              "production bridge integration should route action calls through the live execution service payload");
  assert_true(query_envelope.tool_result.has_value() &&
                  query_envelope.tool_result->success.value_or(false),
              "production bridge integration should keep the query path successful");
  assert_true(query_envelope.tool_result->payload.has_value() &&
                  query_envelope.tool_result->payload->find("\"capability_id\":\"agent.dataset\"") != std::string::npos &&
                  query_envelope.tool_result->payload->find("\"projection\":\"default\"") != std::string::npos,
              "production bridge integration should route query calls through the live data service payload");
  assert_true(action_envelope.observation_digest.has_value() &&
                  action_envelope.observation_digest->citations.has_value() &&
                  contains_string(*action_envelope.observation_digest->citations, "route_kind:builtin"),
              "production bridge integration should preserve the builtin route citation while swapping the backend implementation");
}

}  // namespace

int main() {
  try {
    test_tool_services_production_bridge_uses_live_services_backend();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}