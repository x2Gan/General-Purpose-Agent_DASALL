#include <exception>
#include <iostream>
#include <string>

#include "IDiagnosticsService.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_diagnostics_types_freeze_read_only_command_and_local_export_contract() {
  using dasall::infra::diagnostics::DiagnosticsCommand;
  using dasall::infra::diagnostics::DiagnosticsSnapshot;
  using dasall::infra::diagnostics::ExportFormat;
  using dasall::infra::diagnostics::ExportTarget;
  using dasall::infra::diagnostics::RedactionProfile;
  using dasall::infra::diagnostics::SnapshotExportRequest;
  using dasall::tests::support::assert_true;

  const DiagnosticsCommand command{
      .command_id = std::string("diag-cmd-001"),
      .command_name = std::string("health.snapshot"),
      .args = {},
      .request_scope = std::string("runtime"),
      .timeout_ms = 3000,
      .actor_ref = std::string("ops-user"),
  };
  assert_true(command.is_read_only_whitelisted(),
              "diagnostics command should stay inside the frozen read-only whitelist");

  const DiagnosticsSnapshot snapshot{
      .snapshot_id = std::string("snapshot-001"),
      .command = command,
      .collected_at = std::string("2026-03-27T11:00:00Z"),
      .summary = std::string("health and queue evidence collected"),
      .evidence_refs = {"logs://diag/001", "health://snapshot/001"},
      .redaction_profile = RedactionProfile::Strict,
      .exporter_hint = std::string("local_file"),
  };
  assert_true(snapshot.is_valid(),
              "diagnostics snapshot should remain valid once command, evidence refs, and redaction profile are frozen");

  const SnapshotExportRequest request{
      .snapshot_id = snapshot.snapshot_id,
      .target = ExportTarget::LocalFile,
      .format = ExportFormat::Json,
      .target_ref = std::string("local://diagnostics/snapshot-001.json"),
  };
  assert_true(request.is_valid(),
              "local diagnostics export request should remain valid for the minimal export contract");
}

void test_diagnostics_types_reject_non_whitelisted_or_remote_unsafe_requests() {
  using dasall::infra::diagnostics::DiagnosticsCommand;
  using dasall::infra::diagnostics::ExportFormat;
  using dasall::infra::diagnostics::ExportTarget;
  using dasall::infra::diagnostics::SnapshotExportRequest;
  using dasall::tests::support::assert_true;

  const DiagnosticsCommand invalid_command{
      .command_id = std::string("diag-cmd-002"),
      .command_name = std::string("secret.dump"),
      .args = {},
      .request_scope = std::string("runtime"),
      .timeout_ms = 3000,
      .actor_ref = std::string("ops-user"),
  };
  assert_true(!invalid_command.is_read_only_whitelisted(),
              "diagnostics command should reject names outside the frozen read-only whitelist");

  const SnapshotExportRequest invalid_export{
      .snapshot_id = std::string("snapshot-remote"),
      .target = ExportTarget::RemoteUpload,
      .format = ExportFormat::Unspecified,
      .target_ref = std::string(),
  };
  assert_true(!invalid_export.is_valid(),
              "diagnostics export request should reject unspecified format and empty remote target references");
}

}  // namespace

int main() {
  try {
    test_diagnostics_types_freeze_read_only_command_and_local_export_contract();
    test_diagnostics_types_reject_non_whitelisted_or_remote_unsafe_requests();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}