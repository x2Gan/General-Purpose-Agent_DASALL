#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "InfraContext.h"
#include "diagnostics/CommandExecutor.h"
#include "diagnostics/CommandRegistry.h"
#include "diagnostics/CommandPolicyGuard.h"
#include "diagnostics/IDiagnosticsPolicyGuard.h"
#include "policy/ISecurityPolicyManager.h"
#include "support/TestAssertions.h"

namespace {

class StaticSecurityPolicyManager final : public dasall::infra::policy::ISecurityPolicyManager {
 public:
  explicit StaticSecurityPolicyManager(dasall::infra::policy::PolicyDecision decision)
      : decision_(decision) {}

  [[nodiscard]] dasall::infra::policy::PolicyOpResult load_policy(
      const dasall::infra::policy::PolicyBundle&) override {
    return dasall::infra::policy::PolicyOpResult::success(std::string("policy-snapshot-013"), 13);
  }

  [[nodiscard]] dasall::infra::policy::PolicyOpResult apply_patch(
      const dasall::infra::policy::PolicyPatch&) override {
    return dasall::infra::policy::PolicyOpResult::success(std::string("policy-snapshot-013"), 13);
  }

  [[nodiscard]] dasall::infra::policy::ValidationReport dry_run_patch(
      const dasall::infra::policy::PolicyPatch&) override {
    return dasall::infra::policy::ValidationReport{};
  }

  [[nodiscard]] dasall::infra::policy::PolicySnapshot snapshot() const override {
    return dasall::infra::policy::PolicySnapshot{
        .snapshot_id = std::string("policy-snapshot-013"),
        .generation = 13,
        .version = std::string("policy-v13"),
        .mode = dasall::infra::policy::PolicyMode::Enforced,
        .effective_rules = {dasall::infra::policy::PolicyRuleDescriptor{
            .rule_id = std::string("diag-policy-rule-013"),
            .domain = dasall::infra::policy::PolicyDomain::DiagnosticsCommand,
            .subject = std::string("diagnostics"),
            .action = std::string("execute"),
            .target_selector = std::string("health.snapshot"),
            .effect = decision_ == dasall::infra::policy::PolicyDecision::Allow
                          ? dasall::infra::policy::PolicyEffect::Allow
                          : dasall::infra::policy::PolicyEffect::Deny,
            .priority = 1,
            .mode = dasall::infra::policy::PolicyMode::Enforced,
            .conditions = {std::string("scope=runtime")},
            .reason_code = std::string("diagnostics_policy_eval"),
        }},
        .created_at = std::string("2026-04-07T18:00:00Z"),
        .source_chain = {std::string("defaults"), std::string("profile:desktop_full")},
        .last_known_good_ref = std::string("policy-snapshot-012"),
    };
  }

  [[nodiscard]] dasall::infra::policy::PolicyOpResult rollback(const std::string& snapshot_id) override {
    return dasall::infra::policy::PolicyOpResult::success(snapshot_id.empty() ? std::string("policy-snapshot-012")
                                                                                : snapshot_id,
                                                          12,
                                                          true);
  }

  [[nodiscard]] dasall::infra::policy::PolicyDecisionRef evaluate(
      const dasall::infra::policy::PolicyQueryContext& query) const override {
    last_query_ = query;
    ++evaluate_calls_;

    return dasall::infra::policy::PolicyDecisionRef{
        .decision = decision_,
        .reason_code = decision_ == dasall::infra::policy::PolicyDecision::Allow
                           ? std::string("diagnostics_allowed")
                           : std::string("diagnostics_denied"),
        .matched_rule_ids = {std::string("diag-policy-rule-013")},
        .snapshot_id = std::string("policy-snapshot-013"),
        .generation = 13,
        .evidence_ref = std::string("audit:policy/diagnostics/013"),
        .warnings = {},
    };
  }

  [[nodiscard]] const dasall::infra::policy::PolicyQueryContext& last_query() const {
    return last_query_;
  }

  [[nodiscard]] int evaluate_calls() const {
    return evaluate_calls_;
  }

 private:
  dasall::infra::policy::PolicyDecision decision_;
  mutable dasall::infra::policy::PolicyQueryContext last_query_{};
  mutable int evaluate_calls_ = 0;
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
  using dasall::infra::diagnostics::CommandDecision;
  using dasall::infra::diagnostics::CommandPolicyGuard;
  using dasall::infra::diagnostics::IDiagnosticsPolicyGuard;

  static_assert(std::is_same_v<decltype(&IDiagnosticsPolicyGuard::authorize),
                               CommandDecision (IDiagnosticsPolicyGuard::*)(
                                   const dasall::infra::diagnostics::DiagnosticsCommand&,
                                   const dasall::infra::InfraContext&)>);

  dasall::infra::diagnostics::CommandRegistry registry;
  StaticSecurityPolicyManager policy_manager(dasall::infra::policy::PolicyDecision::Allow);
  CommandPolicyGuard policy_guard(policy_manager);

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
  assert_equal("diagnostics",
               policy_manager.last_query().module,
               "policy guard should project diagnostics module into PolicyQueryContext");
  assert_equal("execute",
               policy_manager.last_query().operation,
               "policy guard should keep the diagnostics execute action stable in PolicyQueryContext");
  assert_equal("diagnostics_command",
               policy_manager.last_query().target_type,
               "policy guard should evaluate diagnostics commands under a dedicated target_type");
  assert_equal("health.snapshot",
               policy_manager.last_query().target_ref,
               "policy guard should use command_name as the policy target_ref");
}

void test_policy_guard_keeps_denial_surface_observable() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  dasall::infra::diagnostics::CommandRegistry registry;
  StaticSecurityPolicyManager policy_manager(dasall::infra::policy::PolicyDecision::Deny);
  dasall::infra::diagnostics::CommandPolicyGuard policy_guard(policy_manager);

  const auto validation = registry.validate(make_command("queue.stats", {std::string("--queue=main")}));
  assert_true(validation.accepted && validation.is_valid(),
              "registry should accept queue.stats before policy denial checks");

  const auto denied = policy_guard.authorize(validation.normalized_command, dasall::infra::InfraContext{});
  assert_true(!denied.allowed && denied.is_valid() && denied.mapped_result_code().has_value(),
              "policy guard denial should remain traceable through CommandDecision only");
  assert_equal("missing-request-id",
               denied.denied_rule_id,
               "policy guard denial should retain a stable denied_rule_id for unknown context");
  assert_equal(0,
               policy_manager.evaluate_calls(),
               "policy guard should short-circuit missing request context before querying the policy manager");
}

void test_policy_guard_maps_policy_denies_to_stable_command_decisions() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  dasall::infra::diagnostics::CommandRegistry registry;
  StaticSecurityPolicyManager policy_manager(dasall::infra::policy::PolicyDecision::Deny);
  dasall::infra::diagnostics::CommandPolicyGuard policy_guard(policy_manager);

  const auto validation = registry.validate(make_command("thread.dump", {std::string("--limit=5")}));
  assert_true(validation.accepted && validation.is_valid(),
              "registry should accept thread.dump before policy deny checks");

  const auto denied = policy_guard.authorize(
      validation.normalized_command,
      dasall::infra::InfraContext{
          .request_id = std::string("req-diag-014"),
          .session_id = std::string("session-diag-014"),
          .trace_id = std::string("trace-diag-014"),
          .task_id = std::string("task-diag-014"),
          .parent_task_id = std::string("parent-diag-014"),
          .lease_id = std::string("lease-diag-014"),
      });

  assert_true(!denied.allowed && denied.is_valid() && denied.policy_ref.find("policy://snapshots/") == 0,
              "policy guard should translate policy manager denies into stable diagnostics policy refs");
  assert_equal("diag-policy-rule-013",
               denied.denied_rule_id,
               "policy guard should surface the matched deny rule as denied_rule_id");
}

void test_executor_runs_after_policy_allow() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  dasall::infra::diagnostics::CommandRegistry registry;
  StaticSecurityPolicyManager policy_manager(dasall::infra::policy::PolicyDecision::Allow);
  dasall::infra::diagnostics::CommandPolicyGuard policy_guard(policy_manager);
  dasall::infra::diagnostics::CommandExecutor executor;

  const auto validation = registry.validate(make_command("queue.stats", {std::string("--queue=main")}));
  assert_true(validation.accepted && validation.is_valid(),
              "registry should accept queue.stats before executor checks");

  const auto decision = policy_guard.authorize(
      validation.normalized_command,
      dasall::infra::InfraContext{
          .request_id = std::string("req-diag-015"),
          .session_id = std::string("session-diag-015"),
          .trace_id = std::string("trace-diag-015"),
          .task_id = std::string("task-diag-015"),
          .parent_task_id = std::string("parent-diag-015"),
          .lease_id = std::string("lease-diag-015"),
      });
  assert_true(decision.allowed && decision.is_valid(),
              "policy guard should allow queue.stats before executor runs");

  const auto execution = executor.execute(validation.normalized_command);
  assert_true(execution.executed && execution.is_valid(),
              "executor should produce a structured success result for allowed diagnostics commands");
  assert_equal("diagnostics executor queue stats",
               execution.summary,
               "executor should keep a stable queue.stats summary for downstream snapshot assembly");
}

}  // namespace

int main() {
  try {
    test_registry_output_can_flow_to_policy_guard();
    test_policy_guard_keeps_denial_surface_observable();
    test_policy_guard_maps_policy_denies_to_stable_command_decisions();
    test_executor_runs_after_policy_allow();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}