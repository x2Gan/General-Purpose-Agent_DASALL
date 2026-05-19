#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "builtin/terminal/AgentTerminalTool.h"
#include "policy/ToolPolicyGate.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] bool contains_string(const std::vector<std::string>& values,
                                   const std::string& expected) {
  return std::find(values.begin(), values.end(), expected) != values.end();
}

[[nodiscard]] dasall::tools::ToolPolicyView make_policy_view() {
  return dasall::tools::ToolPolicyView{
      .effective_profile_id = "desktop_full",
      .safe_mode_enabled = true,
      .high_risk_confirmation_required = true,
      .audit_level = "full",
      .allowed_tool_domains = {"builtin"},
      .tool_visibility_rules = {"builtin:all"},
  };
}

[[nodiscard]] dasall::tools::ToolAdmissionRequest make_admission_request(
    const dasall::contracts::ToolDescriptor& descriptor,
    bool confirmation_present) {
  return dasall::tools::ToolAdmissionRequest{
      .tool_name = descriptor.tool_name.value_or(std::string()),
      .required_scopes = descriptor.required_scopes.value_or(std::vector<std::string>{}),
      .caller_domain = std::string("builtin"),
      .high_risk = descriptor.category == dasall::contracts::ToolCategory::Action &&
                   !descriptor.is_read_only.value_or(false),
      .confirmation_present = confirmation_present,
      .route_proven = true,
  };
}

[[nodiscard]] dasall::contracts::ToolIR make_tool_ir() {
  return dasall::contracts::ToolIR{
      .request_id = std::string("req-agent-terminal"),
      .tool_call_id = std::string("call-agent-terminal"),
      .tool_name = std::string("agent.terminal"),
      .operation = dasall::contracts::ToolIROperation::Invoke,
      .normalized_arguments = std::string("{\"command\":\"echo terminal\"}"),
      .route = dasall::contracts::ToolIRRoute::LocalTool,
      .timeout_ms = 2200U,
      .idempotency_key = std::string("idem-agent-terminal"),
      .priority = dasall::contracts::ToolIRPriority::Normal,
      .goal_id = std::string("goal-agent-terminal"),
      .worker_task_id = std::string("worker-agent-terminal"),
  };
}

[[nodiscard]] dasall::services::ServiceCallContext make_context() {
  return dasall::services::ServiceCallContext{
      .request_id = std::string("req-agent-terminal"),
      .session_id = std::string("session-agent-terminal"),
      .trace_id = std::string("trace-agent-terminal"),
      .tool_call_id = std::string("call-agent-terminal"),
      .goal_id = std::string("goal-agent-terminal"),
      .budget_guard = std::nullopt,
      .deadline_ms = 2200U,
  };
}

void test_agent_terminal_descriptor_stays_high_risk_and_confirmation_gated() {
  dasall::tools::policy::ToolPolicyGate gate;
  const auto descriptor = dasall::tools::builtin::terminal::build_descriptor();

  assert_equal(std::string("agent.terminal"), descriptor.tool_name.value_or(""),
               "agent.terminal wrapper should publish the stable builtin tool name");
  assert_true(!descriptor.is_read_only.value_or(true),
              "agent.terminal wrapper should remain non-read-only so policy treats it as high risk");
  assert_true(descriptor.supports_compensation.value_or(false),
              "agent.terminal wrapper should keep compensation available for action side effects");
  assert_equal(std::string("schema://tools/agent.terminal/input/v1"),
               descriptor.input_schema_ref.value_or(""),
               "agent.terminal wrapper should own the builtin input schema ref");
  assert_equal(std::string("schema://tools/agent.terminal/output/v1"),
               descriptor.output_schema_ref.value_or(""),
               "agent.terminal wrapper should own the builtin output schema ref");
  assert_true(descriptor.required_scopes.has_value() &&
                  contains_string(*descriptor.required_scopes, "tools.execute"),
              "agent.terminal wrapper should require tools.execute scope");

  const auto denied = gate.evaluate(make_admission_request(descriptor, false), make_policy_view());
  assert_true(!denied.allowed(),
              "agent.terminal should be denied when high-risk confirmation is required but absent");
  assert_equal(std::string("policy.confirmation_required"), denied.reason_code,
               "agent.terminal denial should surface confirmation_required");

  const auto allowed = gate.evaluate(make_admission_request(descriptor, true), make_policy_view());
  assert_true(allowed.allowed(),
              "agent.terminal should pass policy once confirmation evidence is present");
}

void test_agent_terminal_wrapper_maps_action_arguments_to_execution_request() {
  const auto request = dasall::tools::builtin::terminal::build_action_request(
      make_tool_ir(),
      make_context());

  assert_equal(std::string("req-agent-terminal"), request.context.request_id,
               "agent.terminal action mapping should preserve request_id");
  assert_equal(std::string("trace-agent-terminal"), request.context.trace_id,
               "agent.terminal action mapping should preserve trace correlation");
  assert_equal(std::string("agent.terminal"), request.target.capability_id,
               "agent.terminal action mapping should preserve the builtin capability id");
  assert_equal(std::string("builtin:agent.terminal"), request.target.target_id,
               "agent.terminal action mapping should derive a stable builtin target id");
  assert_equal(std::string("agent.terminal"), request.action,
               "agent.terminal action mapping should keep the stable action name");
  assert_equal(std::string("{\"command\":\"echo terminal\"}"), request.arguments_json,
               "agent.terminal action mapping should forward normalized arguments unchanged");
  assert_equal(std::string("idem-agent-terminal"), request.idempotency_key.value_or(""),
               "agent.terminal action mapping should preserve idempotency_key");
}

}  // namespace

int main() {
  try {
    test_agent_terminal_descriptor_stays_high_risk_and_confirmation_gated();
    test_agent_terminal_wrapper_maps_action_arguments_to_execution_request();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}