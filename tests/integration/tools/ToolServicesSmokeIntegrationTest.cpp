#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "RuntimePolicySnapshot.h"
#include "ToolManager.h"
#include "execution/BuiltinExecutorLane.h"
#include "registry/ToolRegistry.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] bool contains_string(const std::vector<std::string>& values,
                                                                     const std::string& expected) {
    return std::find(values.begin(), values.end(), expected) != values.end();
}

[[nodiscard]] dasall::contracts::ToolRequest make_action_request() {
  return dasall::contracts::ToolRequest{
            .request_id = std::string("req-tool-smoke-action"),
            .tool_call_id = std::string("call-tool-smoke-action"),
      .tool_name = std::string("agent.terminal"),
      .invocation_kind = dasall::contracts::ToolInvocationKind::Action,
      .arguments_payload = std::string("{\"command\":\"echo smoke\"}"),
      .created_at = 1000,
            .goal_id = std::string("goal-tool-smoke-action"),
            .worker_task_id = std::string("worker-tool-smoke-action"),
      .runtime_budget = std::nullopt,
      .timeout_ms = 2500U,
            .idempotency_key = std::string("idem-tool-smoke-action"),
            .tags = std::vector<std::string>{"integration", "tools", "action"},
  };
}

[[nodiscard]] dasall::contracts::ToolRequest make_query_request() {
    return dasall::contracts::ToolRequest{
            .request_id = std::string("req-tool-smoke-query"),
            .tool_call_id = std::string("call-tool-smoke-query"),
            .tool_name = std::string("agent.dataset"),
            .invocation_kind = dasall::contracts::ToolInvocationKind::InformationQuery,
            .arguments_payload = std::string("{\"scope\":\"session\"}"),
            .created_at = 1001,
            .goal_id = std::string("goal-tool-smoke-query"),
            .worker_task_id = std::string("worker-tool-smoke-query"),
            .runtime_budget = std::nullopt,
            .timeout_ms = 2500U,
            .idempotency_key = std::string("idem-tool-smoke-query"),
            .tags = std::vector<std::string>{"integration", "tools", "query"},
    };
}

[[nodiscard]] dasall::contracts::ToolRequest make_missing_request() {
    return dasall::contracts::ToolRequest{
            .request_id = std::string("req-tool-smoke-missing"),
            .tool_call_id = std::string("call-tool-smoke-missing"),
            .tool_name = std::string("tool.missing"),
            .invocation_kind = dasall::contracts::ToolInvocationKind::Action,
            .arguments_payload = std::string("{}"),
            .created_at = 1002,
            .goal_id = std::string("goal-tool-smoke-missing"),
            .worker_task_id = std::string("worker-tool-smoke-missing"),
            .runtime_budget = std::nullopt,
            .timeout_ms = 2500U,
            .idempotency_key = std::string("idem-tool-smoke-missing"),
            .tags = std::vector<std::string>{"integration", "tools", "negative"},
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

[[nodiscard]] dasall::tools::ToolManager make_smoke_manager() {
  auto registry = std::make_shared<dasall::tools::registry::ToolRegistry>();
  assert_true(registry->register_builtin(make_query_descriptor()),
              "tools integration smoke should register a builtin query descriptor for query-path verification");

  auto builtin_lane = std::make_shared<dasall::tools::execution::BuiltinExecutorLane>(
      dasall::tools::execution::BuiltinExecutorLaneDependencies{
          .registry = registry,
          .service_bridge = nullptr,
          .execution_service = nullptr,
          .data_service = nullptr,
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

void assert_closed_loop(const dasall::tools::ToolInvocationEnvelope& envelope,
                        const std::string& expected_tool_name,
                        const std::string& expected_payload_fragment,
                        const std::string& expected_tag) {
  assert_true(envelope.tool_result.has_value(),
              "tools integration smoke should always produce a ToolResult on builtin closed-loop paths");
  assert_true(envelope.tool_result->success.value_or(false),
              "tools integration smoke should keep builtin closed-loop paths successful");
  assert_true(envelope.observation.has_value() && envelope.observation_digest.has_value(),
              "tools integration smoke should project ToolResult into Observation and ObservationDigest together");
  assert_true(envelope.route_facts.has_value() && envelope.evidence_refs.has_value(),
              "tools integration smoke should emit route facts and evidence references together with the projection");
  assert_true(envelope.tool_result->tool_name.has_value() &&
                  *envelope.tool_result->tool_name == expected_tool_name,
              "tools integration smoke should preserve the resolved builtin tool name in ToolResult");
  assert_true(envelope.tool_result->payload.has_value() &&
                  envelope.tool_result->payload->find(expected_payload_fragment) != std::string::npos,
              "tools integration smoke should preserve the expected builtin service payload fragment");
  assert_true(envelope.observation->observation_id.has_value() &&
                  envelope.observation_digest->observation_id.has_value() &&
                  envelope.observation->observation_id == envelope.observation_digest->observation_id,
              "tools integration smoke should keep Observation and ObservationDigest linked by the same observation id");
  assert_true(envelope.observation->success == envelope.tool_result->success &&
                  envelope.observation->payload == envelope.tool_result->payload,
              "tools integration smoke should fold ToolResult success and payload into Observation without drift");
  assert_true(envelope.observation->source == dasall::contracts::ObservationSource::ToolExecution &&
                  envelope.observation_digest->source == dasall::contracts::ObservationSource::ToolExecution,
              "tools integration smoke should keep Observation and ObservationDigest on the ToolExecution source channel");
  assert_true(envelope.observation_digest->summary.has_value() &&
                  !envelope.observation_digest->summary->empty() &&
                  envelope.observation_digest->key_facts.has_value() &&
                  !envelope.observation_digest->key_facts->empty(),
              "tools integration smoke should produce a non-empty digest summary and key facts");
  assert_true(envelope.observation_digest->citations.has_value() &&
                  contains_string(*envelope.observation_digest->citations,
                                  std::string("tool_call:") + *envelope.tool_result->tool_call_id) &&
                  contains_string(*envelope.observation_digest->citations, "route_kind:builtin"),
              "tools integration smoke should keep tool call and builtin route citations in ObservationDigest");
  assert_true(envelope.observation_digest->confidence.has_value() &&
                  *envelope.observation_digest->confidence >= 0.1f &&
                  *envelope.observation_digest->confidence <= 1.0f,
              "tools integration smoke should emit a bounded digest confidence value");
  assert_true(envelope.observation_digest->tags.has_value() &&
                  contains_string(*envelope.observation_digest->tags, expected_tag),
              "tools integration smoke should preserve request tags in the digest projection");
  assert_equal(std::string("builtin"), *envelope.route_facts->route_kind,
               "tools integration smoke should use the builtin route by default");
  assert_true(!envelope.failure_reason_code.has_value(),
              "tools integration smoke should not surface a failure reason on successful builtin closures");
}

void test_tool_services_smoke_covers_builtin_action_and_query_closure() {
  auto manager = make_smoke_manager();
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

  const auto action_envelope = manager.invoke(make_action_request(), context);
  const auto query_envelope = manager.invoke(make_query_request(), context);

  assert_closed_loop(action_envelope,
                     "agent.terminal",
                     "\"action\":\"agent.terminal\"",
                     "action");
  assert_closed_loop(query_envelope,
                     "agent.dataset",
                     "\"dataset\":\"agent.dataset\"",
                     "query");
}

void test_tool_services_smoke_keeps_missing_descriptor_fail_closed() {
  auto manager = make_smoke_manager();
  const auto snapshot = make_snapshot();
  const dasall::tools::ToolInvocationContext context{
      .caller_domain = std::string("runtime.main"),
      .session_id = std::string("session-tool-smoke-negative"),
      .profile_snapshot = &snapshot,
      .trace = {
          .trace_id = std::string("trace-tool-smoke-negative"),
          .span_id = std::string("span-tool-smoke-negative"),
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::vector<dasall::tools::ToolConfirmationFact>{
          dasall::tools::ToolConfirmationFact{
              .confirmation_id = std::string("confirm-tool-smoke-negative"),
              .subject_ref = std::string("goal://tool-smoke-negative"),
              .proof_type = std::string("user.approved"),
              .confirmed_at_ms = 901,
          }},
  };

  const auto envelope = manager.invoke(make_missing_request(), context);

  assert_true(envelope.tool_result.has_value() &&
                  !envelope.tool_result->success.value_or(true),
              "tools integration smoke should fail closed when the descriptor is missing");
  assert_true(!envelope.has_projection(),
              "tools integration smoke should not fabricate Observation or ObservationDigest on descriptor-missing preflight failures");
  assert_true(envelope.failure_reason_code.has_value() &&
                  envelope.failure_reason_code->find("descriptor_missing") != std::string::npos,
              "tools integration smoke should expose the descriptor-missing failure reason for negative gating");
}

}  // namespace

int main() {
  try {
        test_tool_services_smoke_covers_builtin_action_and_query_closure();
        test_tool_services_smoke_keeps_missing_descriptor_fail_closed();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}