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

[[nodiscard]] dasall::contracts::ToolDescriptor make_descriptor(bool supports_compensation) {
  return dasall::contracts::ToolDescriptor{
      .tool_name = std::string("agent.terminal"),
      .display_name = std::string("Agent Terminal"),
      .category = dasall::contracts::ToolCategory::Action,
      .capability_tier = dasall::contracts::ToolCapabilityTier::Stable,
      .is_read_only = false,
      .supports_compensation = supports_compensation,
      .default_timeout_ms = 2500U,
      .input_schema_ref = std::string("schema://tools/agent.terminal/input/v1"),
      .output_schema_ref = std::string("schema://tools/agent.terminal/output/v1"),
      .required_scopes = std::vector<std::string>{"tools.execute"},
      .tags = std::vector<std::string>{"builtin"},
      .version = std::string("1.0.0"),
  };
}

[[nodiscard]] std::shared_ptr<dasall::tools::registry::ToolRegistry> make_registry(
    bool supports_compensation) {
  auto registry = std::make_shared<dasall::tools::registry::ToolRegistry>();
  assert_true(registry->register_builtin(make_descriptor(supports_compensation)),
              "compensation pipeline unit test should register the builtin descriptor");
  return registry;
}

[[nodiscard]] dasall::tools::ToolInvocationContext make_context(
    const dasall::profiles::RuntimePolicySnapshot& snapshot,
    bool confirmed) {
  return dasall::tools::ToolInvocationContext{
      .caller_domain = std::string("runtime.main"),
      .session_id = std::string("session-comp-pipeline"),
      .profile_snapshot = &snapshot,
      .trace = {
          .trace_id = std::string("trace-comp-pipeline"),
          .span_id = std::string("span-comp-pipeline"),
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = confirmed
          ? std::optional<std::vector<dasall::tools::ToolConfirmationFact>>(
                std::vector<dasall::tools::ToolConfirmationFact>{
                    dasall::tools::ToolConfirmationFact{
                        .confirmation_id = std::string("confirm-comp-pipeline"),
                        .subject_ref = std::string("goal://comp-pipeline"),
                        .proof_type = std::string("user.approved"),
                        .confirmed_at_ms = 900,
                    },
                })
          : std::nullopt,
  };
}

[[nodiscard]] dasall::tools::CompensationRequest make_request() {
  return dasall::tools::CompensationRequest{
      .tool_call_id = std::string("call-comp-pipeline"),
      .compensation_action = std::string("safe_mode.exit"),
      .target_ref = std::string("tool://agent.terminal/call-comp-pipeline"),
      .reason_code = std::string("manual_recovery"),
      .evidence_refs = std::vector<std::string>{"recovery://call-comp-pipeline"},
  };
}

void test_compensation_pipeline_executes_through_builtin_route() {
  bool called = false;
  dasall::tools::manager::ToolManagerDependencies dependencies;
  dependencies.registry = make_registry(true);
  dependencies.compensation_executor = [&called](const auto& tool_ir,
                                                 const auto& request,
                                                 const auto&) {
    called = true;
    assert_equal(std::string("agent.terminal"), tool_ir.tool_name.value_or(""),
                 "compensation pipeline should resolve tool_name from target_ref capability");
    assert_equal(std::string("safe_mode.exit"),
                 request.compensation_action.value_or(""),
                 "compensation pipeline should preserve compensation_action into the executor");
    return dasall::contracts::ToolResult{
        .request_id = tool_ir.request_id,
        .tool_call_id = request.tool_call_id,
        .tool_name = tool_ir.tool_name,
        .success = true,
        .payload = std::string("{\"status\":\"compensated\"}"),
        .error = std::nullopt,
        .side_effects = std::vector<std::string>{"terminal.compensated"},
        .completed_at = 2000,
        .duration_ms = 8,
        .goal_id = tool_ir.goal_id,
        .worker_task_id = tool_ir.worker_task_id,
        .tags = std::vector<std::string>{"compensation"},
    };
  };

  dasall::tools::ToolManager manager(std::move(dependencies));
  const auto snapshot = make_snapshot(true);
  const auto envelope = manager.compensate(make_request(), make_context(snapshot, true));

  assert_true(called,
              "compensation pipeline should dispatch to the configured compensation executor");
  assert_true(envelope.tool_result.has_value() &&
                  envelope.tool_result->success.value_or(false),
              "compensation pipeline should surface successful compensation results");
  assert_true(envelope.has_projection(),
              "compensation pipeline should still project successful compensation results");
  assert_true(envelope.route_facts.has_value() &&
                  envelope.route_facts->route_kind == std::string("builtin"),
              "compensation pipeline should expose builtin route facts");
  assert_true(!envelope.failure_reason_code.has_value(),
              "successful compensation should not retain a failure reason");
  assert_true(envelope.evidence_refs.has_value() &&
                  std::find(envelope.evidence_refs->begin(), envelope.evidence_refs->end(),
                            std::string("recovery://call-comp-pipeline")) !=
                      envelope.evidence_refs->end(),
              "compensation pipeline should preserve incoming evidence refs on the envelope");
}

void test_compensation_pipeline_denies_high_risk_calls_without_confirmation() {
  bool called = false;
  dasall::tools::manager::ToolManagerDependencies dependencies;
  dependencies.registry = make_registry(true);
  dependencies.compensation_executor = [&called](const auto&, const auto&, const auto&) {
    called = true;
    return dasall::contracts::ToolResult{};
  };

  dasall::tools::ToolManager manager(std::move(dependencies));
  const auto snapshot = make_snapshot(true);
  const auto envelope = manager.compensate(make_request(), make_context(snapshot, false));

  assert_true(!called,
              "policy-denied compensation should fail before reaching the compensation executor");
  assert_true(envelope.failure_reason_code.has_value() &&
                  *envelope.failure_reason_code == std::string("policy.confirmation_required"),
              "compensation pipeline should reuse the policy gate for high-risk confirmation checks");
  assert_true(!envelope.has_projection(),
              "policy-denied compensation should not fabricate a projection");
}

void test_compensation_pipeline_rejects_descriptors_without_compensation_support() {
  bool called = false;
  dasall::tools::manager::ToolManagerDependencies dependencies;
  dependencies.registry = make_registry(false);
  dependencies.compensation_executor = [&called](const auto&, const auto&, const auto&) {
    called = true;
    return dasall::contracts::ToolResult{};
  };

  dasall::tools::ToolManager manager(std::move(dependencies));
  const auto snapshot = make_snapshot(false);
  const auto envelope = manager.compensate(make_request(), make_context(snapshot, true));

  assert_true(!called,
              "unsupported compensation should fail before reaching the compensation executor");
  assert_true(envelope.failure_reason_code.has_value() &&
                  *envelope.failure_reason_code == std::string("tool.manager.compensation_unsupported"),
              "compensation pipeline should fail closed when the descriptor does not advertise compensation support");
}

}  // namespace

int main() {
  try {
    test_compensation_pipeline_executes_through_builtin_route();
    test_compensation_pipeline_denies_high_risk_calls_without_confirmation();
    test_compensation_pipeline_rejects_descriptors_without_compensation_support();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}