#include <exception>
#include <iostream>
#include <string>

#include "InfraContext.h"
#include "diagnostics/CommandRegistry.h"
#include "diagnostics/IDiagnosticsPolicyGuard.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

class StubDiagnosticsPolicyGuard final : public dasall::infra::diagnostics::IDiagnosticsPolicyGuard {
 public:
  [[nodiscard]] dasall::infra::diagnostics::CommandDecision authorize(
      const dasall::infra::diagnostics::DiagnosticsCommand& command,
      const dasall::infra::InfraContext& context) override {
    if (command.is_read_only_whitelisted() &&
        context.request_id != dasall::infra::InfraContext::kUnknownIdentifier) {
      return dasall::infra::diagnostics::CommandDecision{
          .allowed = true,
          .reason_code = std::string(),
          .policy_ref = std::string("policy://diagnostics/readonly"),
          .denied_rule_id = std::string(),
      };
    }

    return dasall::infra::diagnostics::CommandDecision{
        .allowed = false,
        .reason_code = std::string("diag_command_denied"),
        .policy_ref = std::string("policy://diagnostics/readonly"),
        .denied_rule_id = std::string("missing-request-id"),
    };
  }
};

[[nodiscard]] dasall::infra::diagnostics::DiagnosticsCommand make_command(
    std::string command_name,
    std::vector<std::string> args = {}) {
  return dasall::infra::diagnostics::DiagnosticsCommand{
      .command_id = std::string("diag-cmd-policy-001"),
      .command_name = std::move(command_name),
      .args = std::move(args),
      .request_scope = std::string("runtime"),
      .timeout_ms = 3000,
      .actor_ref = std::string("ops-user"),
  };
}

void test_registry_output_can_flow_to_policy_guard() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  dasall::infra::diagnostics::CommandRegistry registry;
  StubDiagnosticsPolicyGuard policy_guard;

  const auto validation = registry.validate(make_command("health.snapshot"));
  assert_true(validation.accepted && validation.is_valid(),
              "registry should accept health.snapshot before policy evaluation");
  assert_equal("--summary",
               validation.normalized_command.args.front(),
               "registry should normalize health.snapshot to the frozen summary token");

  const auto decision = policy_guard.authorize(
      validation.normalized_command,
      dasall::infra::InfraContext{
          .request_id = std::string("req-diag-013"),
          .session_id = std::string("session-diag-013"),
          .trace_id = std::string("trace-diag-013"),
          .task_id = std::string("task-diag-013"),
          .parent_task_id = std::string("parent-diag-013"),
          .lease_id = std::string("lease-diag-013"),
      });

  assert_true(decision.allowed && decision.is_valid(),
              "policy guard should accept the normalized diagnostics command handoff when context is explicit");
}

void test_policy_guard_keeps_denial_surface_observable() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  dasall::infra::diagnostics::CommandRegistry registry;
  StubDiagnosticsPolicyGuard policy_guard;

  const auto validation = registry.validate(make_command("queue.stats", {std::string("--queue=main")}));
  assert_true(validation.accepted && validation.is_valid(),
              "registry should accept queue.stats before policy denial checks");

  const auto denied = policy_guard.authorize(validation.normalized_command, dasall::infra::InfraContext{});
  assert_true(!denied.allowed && denied.is_valid() && denied.mapped_result_code().has_value(),
              "policy guard denial should remain traceable through CommandDecision only");
  assert_equal("missing-request-id",
               denied.denied_rule_id,
               "policy guard denial should retain a stable denied_rule_id for unknown context");
}

}  // namespace

int main() {
  try {
    test_registry_output_can_flow_to_policy_guard();
    test_policy_guard_keeps_denial_surface_observable();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}