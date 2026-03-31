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

}  // namespace

int main() {
  try {
    test_diagnostics_command_freezes_required_fields_and_read_only_whitelist();
    test_diagnostics_command_rejects_missing_fields_and_non_whitelisted_names();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}