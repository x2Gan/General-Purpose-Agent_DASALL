#include <exception>
#include <iostream>
#include <string>

#include "diagnostics/DiagnosticsTypes.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_diagnostics_command_freezes_required_fields_and_read_only_whitelist() {
  using dasall::infra::diagnostics::DiagnosticsCommand;
  using dasall::tests::support::assert_true;

  const DiagnosticsCommand command{
      .command_id = std::string("diag-cmd-001"),
      .command_name = std::string("health.snapshot"),
      .args = {std::string("--summary")},
      .request_scope = std::string("runtime"),
      .timeout_ms = 3000,
      .actor_ref = std::string("ops-user"),
  };

  assert_true(command.has_required_fields(),
              "diagnostics command should remain valid once all frozen fields are present");
  assert_true(command.has_whitelisted_command_name(),
              "diagnostics command should expose the frozen read-only whitelist check");
  assert_true(command.is_read_only_whitelisted(),
              "diagnostics command should remain executable only when required fields and whitelist both pass");
}

void test_diagnostics_command_rejects_missing_fields_and_non_whitelisted_names() {
  using dasall::infra::diagnostics::DiagnosticsCommand;
  using dasall::tests::support::assert_true;

  const DiagnosticsCommand missing_actor{
      .command_id = std::string("diag-cmd-002"),
      .command_name = std::string("queue.stats"),
      .args = {},
      .request_scope = std::string("runtime"),
      .timeout_ms = 3000,
      .actor_ref = std::string(),
  };
  assert_true(!missing_actor.has_required_fields(),
              "diagnostics command should reject empty actor_ref even when command_name is allowlisted");

  const DiagnosticsCommand non_whitelisted{
      .command_id = std::string("diag-cmd-003"),
      .command_name = std::string("secret.dump"),
      .args = {},
      .request_scope = std::string("runtime"),
      .timeout_ms = 3000,
      .actor_ref = std::string("ops-user"),
  };
  assert_true(!non_whitelisted.has_whitelisted_command_name(),
              "diagnostics command should reject command_name values outside the frozen read-only whitelist");
  assert_true(!non_whitelisted.is_read_only_whitelisted(),
              "diagnostics command should reject execution when command_name is outside the frozen read-only whitelist");
}

void test_command_decision_freezes_allow_and_deny_semantics() {
  using dasall::contracts::ResultCode;
  using dasall::infra::diagnostics::CommandDecision;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const CommandDecision allowed{
      .allowed = true,
      .reason_code = std::string(),
      .policy_ref = std::string("policy://diagnostics/readonly"),
      .denied_rule_id = std::string(),
  };
  assert_true(allowed.is_valid(),
              "command decision should allow an explicit allow path without deny-only fields");
  assert_true(!allowed.mapped_result_code().has_value(),
              "allowed command decisions should not force a contracts denial result code");

  const CommandDecision denied{
      .allowed = false,
      .reason_code = std::string("diag_command_denied"),
      .policy_ref = std::string("policy://diagnostics/readonly"),
      .denied_rule_id = std::string("readonly-only"),
  };
  assert_true(denied.is_valid(),
              "command decision should keep reason_code, policy_ref, and denied_rule_id for deny paths");
  assert_true(denied.mapped_result_code().has_value(),
              "diagnostics deny decisions should expose a contracts result code mapping");
  assert_equal(static_cast<int>(ResultCode::PolicyDenied),
               static_cast<int>(*denied.mapped_result_code()),
               "diagnostics deny decisions should stay inside contracts policy semantics");
}

void test_command_decision_rejects_unknown_reason_codes_and_allow_path_deny_fields() {
  using dasall::infra::diagnostics::CommandDecision;
  using dasall::tests::support::assert_true;

  const CommandDecision allowed_with_deny_field{
      .allowed = true,
      .reason_code = std::string(),
      .policy_ref = std::string(),
      .denied_rule_id = std::string("readonly-only"),
  };
  assert_true(!allowed_with_deny_field.is_valid(),
              "allow decisions should reject denied_rule_id because it is deny-only state");

  const CommandDecision unknown_reason{
      .allowed = false,
      .reason_code = std::string("diag_unknown"),
      .policy_ref = std::string("policy://diagnostics/readonly"),
      .denied_rule_id = std::string("readonly-only"),
  };
  assert_true(!unknown_reason.is_valid(),
              "deny decisions should reject reason_code values that cannot be mapped into contracts semantics");
}

void test_evidence_bundle_freezes_reference_only_fields() {
  using dasall::infra::diagnostics::EvidenceBundle;
  using dasall::tests::support::assert_true;

  const EvidenceBundle bundle{
      .logs_ref = std::string("logs://diagnostics/001"),
      .metrics_ref = std::string("metrics://diagnostics/001"),
      .health_ref = std::string("health://diagnostics/001"),
      .errors_ref = std::string("errors://diagnostics/001"),
      .artifacts = {std::string("artifact://diagnostics/thread-dump.txt"),
                    std::string("artifact://diagnostics/queue-stats.json")},
  };
  assert_true(bundle.is_valid(),
              "evidence bundle should stay valid when four frozen evidence references are present");
  assert_true(bundle.artifacts.size() == 2,
              "evidence bundle should preserve artifact references as lightweight summaries only");
}

void test_evidence_bundle_rejects_missing_reference_fields() {
  using dasall::infra::diagnostics::EvidenceBundle;
  using dasall::tests::support::assert_true;

  const EvidenceBundle missing_errors{
      .logs_ref = std::string("logs://diagnostics/002"),
      .metrics_ref = std::string("metrics://diagnostics/002"),
      .health_ref = std::string("health://diagnostics/002"),
      .errors_ref = std::string(),
      .artifacts = {},
  };
  assert_true(!missing_errors.is_valid(),
              "evidence bundle should reject missing errors_ref so every retained summary remains traceable");
}

}  // namespace

int main() {
  try {
    test_diagnostics_command_freezes_required_fields_and_read_only_whitelist();
    test_diagnostics_command_rejects_missing_fields_and_non_whitelisted_names();
    test_command_decision_freezes_allow_and_deny_semantics();
    test_command_decision_rejects_unknown_reason_codes_and_allow_path_deny_fields();
    test_evidence_bundle_freezes_reference_only_fields();
    test_evidence_bundle_rejects_missing_reference_fields();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}