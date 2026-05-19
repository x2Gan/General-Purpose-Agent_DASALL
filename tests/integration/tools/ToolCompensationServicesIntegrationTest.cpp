#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
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
      .supports_compensation = true,
      .default_timeout_ms = 30000U,
      .input_schema_ref = std::string("schema://tools/agent.terminal/input/v1"),
      .output_schema_ref = std::string("schema://tools/agent.terminal/output/v1"),
      .required_scopes = std::vector<std::string>{"tools.execute"},
      .tags = std::vector<std::string>{"builtin", "action"},
      .version = std::string("1.0.0"),
  };
}

[[nodiscard]] dasall::contracts::ToolRequest make_action_request() {
  return dasall::contracts::ToolRequest{
      .request_id = std::string("req-tool-comp-int-action"),
      .tool_call_id = std::string("call-tool-comp-int-action"),
      .tool_name = std::string("agent.terminal"),
      .invocation_kind = dasall::contracts::ToolInvocationKind::Action,
      .arguments_payload = std::string("{\"command\":\"echo production\"}"),
      .created_at = 2000,
      .goal_id = std::string("goal-tool-comp-int"),
      .worker_task_id = std::string("worker-tool-comp-int"),
      .runtime_budget = std::nullopt,
      .timeout_ms = 2500U,
      .idempotency_key = std::string("idem-tool-comp-int-action"),
      .tags = std::vector<std::string>{"integration", "tools", "action"},
  };
}

[[nodiscard]] dasall::tools::ToolManager make_manager() {
  const auto snapshot = make_snapshot();
  const auto live_services = dasall::services::compose_live_services(snapshot);
  assert_true(live_services.ok(),
              std::string("compensation services integration should compose live services: ") +
                  live_services.error);

  auto registry = std::make_shared<dasall::tools::registry::ToolRegistry>();
  assert_true(registry->register_builtin(make_action_descriptor()),
              "compensation services integration should register the builtin action descriptor");

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
  dependencies.compensation_executor = [builtin_lane](const auto& tool_ir,
                                                      const auto& request,
                                                      const auto& execution_context) {
    return builtin_lane->dispatch_compensation(tool_ir, request, execution_context);
  };
  return dasall::tools::ToolManager(std::move(dependencies));
}

[[nodiscard]] dasall::tools::ToolInvocationContext make_context(
    const dasall::profiles::RuntimePolicySnapshot& snapshot) {
  return dasall::tools::ToolInvocationContext{
      .caller_domain = std::string("runtime.main"),
      .session_id = std::string("session-tool-comp-int"),
      .profile_snapshot = &snapshot,
      .trace = {
          .trace_id = std::string("trace-tool-comp-int"),
          .span_id = std::string("span-tool-comp-int"),
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::vector<dasall::tools::ToolConfirmationFact>{
          dasall::tools::ToolConfirmationFact{
              .confirmation_id = std::string("confirm-tool-comp-int"),
              .subject_ref = std::string("goal://tool-comp-int"),
              .proof_type = std::string("user.approved"),
              .confirmed_at_ms = 1900,
          }},
  };
}

void test_tool_compensation_services_integration_uses_live_services_backend() {
  auto manager = make_manager();
  const auto snapshot = make_snapshot();
  const auto context = make_context(snapshot);

  const auto action_envelope = manager.invoke(make_action_request(), context);
  assert_true(action_envelope.compensation_hints.has_value() &&
                  action_envelope.compensation_hints->size() == 1U,
              "compensation services integration should surface a parseable compensation hint from the action path");
  const auto compensation_envelope = manager.compensate(
      dasall::tools::CompensationRequest{
          .tool_call_id = std::string("call-tool-comp-int-action"),
          .compensation_action = action_envelope.compensation_hints->front().compensation_action,
          .target_ref = action_envelope.compensation_hints->front().target_ref,
          .reason_code = action_envelope.compensation_hints->front().reason_code,
          .evidence_refs = action_envelope.compensation_hints->front().evidence_refs,
      },
      context);

  assert_true(action_envelope.tool_result.has_value() &&
                  action_envelope.tool_result->success.value_or(false),
              "compensation services integration should keep the action path successful");
  assert_true(compensation_envelope.tool_result.has_value() &&
                  compensation_envelope.tool_result->success.value_or(false),
              "compensation services integration should keep the compensation path successful");
  assert_true(compensation_envelope.tool_result->payload.has_value() &&
                  compensation_envelope.tool_result->payload->find("\"operation\":\"agent.terminal\"") !=
                      std::string::npos,
              "compensation services integration should route compensate() through the live execution service payload");
  assert_true(compensation_envelope.route_facts.has_value() &&
                  compensation_envelope.route_facts->route_kind == std::string("builtin"),
              "compensation services integration should preserve builtin route facts on the compensation path");
  assert_true(compensation_envelope.evidence_refs.has_value() &&
                  std::find(compensation_envelope.evidence_refs->begin(),
                            compensation_envelope.evidence_refs->end(),
                    std::string("agent.terminal.applied")) !=
                      compensation_envelope.evidence_refs->end(),
              "compensation services integration should preserve recovery evidence refs on the envelope");
}

}  // namespace

int main() {
  try {
    test_tool_compensation_services_integration_uses_live_services_backend();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}