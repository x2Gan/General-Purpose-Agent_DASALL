#include <exception>
#include <iostream>
#include <string>

#include "diagnostics/CommandExecutor.h"
#include "diagnostics/EvidenceCollector.h"
#include "diagnostics/IDiagnosticsService.h"
#include "diagnostics/SnapshotAssembler.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] bool contains_ref(const std::vector<std::string>& refs, const std::string& expected) {
  return std::find(refs.begin(), refs.end(), expected) != refs.end();
}

void test_diagnostics_types_freeze_read_only_command_and_local_export_contract() {
  using dasall::infra::diagnostics::DiagnosticsCommand;
  using dasall::infra::diagnostics::DiagnosticsSnapshot;
  using dasall::infra::diagnostics::ExportFormat;
  using dasall::infra::diagnostics::ExportTarget;
  using dasall::infra::diagnostics::RedactionProfile;
  using dasall::infra::diagnostics::SnapshotExportResult;
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
  assert_true(snapshot.is_redaction_ready(),
              "diagnostics snapshot should require whitelisted command, evidence refs, and explicit redaction profile before export");
  assert_true(snapshot.is_valid(),
              "diagnostics snapshot should remain valid once command, evidence refs, and redaction profile are frozen");

  const SnapshotExportRequest request{
      .snapshot_id = snapshot.snapshot_id,
      .target = ExportTarget::LocalFile,
      .format = ExportFormat::Json,
      .target_ref = std::string("local://diagnostics/snapshot-001.jsonl"),
  };
  assert_true(request.is_valid(),
              "local diagnostics export request should remain valid for the minimal export contract");

  const auto export_result = SnapshotExportResult::success(std::string("export-001"),
                                                           ExportTarget::LocalFile,
                                                           ExportFormat::Json,
                                                           256,
                                                           std::string("sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"),
                                                           std::string("2026-03-27T11:01:00Z"));
  assert_true(export_result.is_valid(),
              "snapshot export result should remain valid once export metadata and checksum are frozen");
}

void test_diagnostics_types_reject_non_whitelisted_or_remote_unsafe_requests() {
  using dasall::contracts::ResultCode;
  using dasall::infra::diagnostics::DiagnosticsCommand;
  using dasall::infra::diagnostics::DiagnosticsSnapshot;
  using dasall::infra::diagnostics::ExportFormat;
  using dasall::infra::diagnostics::ExportTarget;
  using dasall::infra::diagnostics::RedactionProfile;
  using dasall::infra::diagnostics::SnapshotExportResult;
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

  const DiagnosticsSnapshot invalid_snapshot{
      .snapshot_id = std::string("snapshot-invalid"),
      .command = invalid_command,
      .collected_at = std::string("2026-03-27T11:05:00Z"),
      .summary = std::string("raw diagnostic output"),
      .evidence_refs = {"logs://diag/invalid"},
      .redaction_profile = RedactionProfile::Compatibility,
      .exporter_hint = std::string("local_file"),
  };
  assert_true(!invalid_snapshot.is_redaction_ready(),
              "diagnostics snapshot should reject redaction readiness when command is not in the read-only whitelist");
  assert_true(!invalid_snapshot.is_valid(),
              "diagnostics snapshot should reject exportable state before the read-only command constraint is satisfied");

  const SnapshotExportRequest invalid_export{
      .snapshot_id = std::string("snapshot-remote"),
      .target = ExportTarget::RemoteUpload,
      .format = ExportFormat::Unspecified,
      .target_ref = std::string(),
  };
  assert_true(!invalid_export.is_valid(),
              "diagnostics export request should reject unspecified format and empty remote target references");

  const auto export_failure = SnapshotExportResult::failure(ResultCode::ProviderTimeout,
                                                            "diagnostics export target is unavailable",
                                                            "diagnostics.export",
                                                            "InMemoryDiagnosticsService");
  assert_true(export_failure.is_valid(),
              "snapshot export failure should remain valid only when it carries a contracts error binding");
  assert_true(export_failure.references_only_contract_error_types(),
              "snapshot export failure should stay inside contracts ResultCode/ErrorInfo types");
}

void test_snapshot_assembler_generates_unique_snapshot_ids_and_binds_evidence_refs() {
  using dasall::infra::diagnostics::CommandExecutionResult;
  using dasall::infra::diagnostics::DiagnosticsCommand;
  using dasall::infra::diagnostics::EvidenceCollector;
  using dasall::infra::diagnostics::SnapshotAssembler;
  using dasall::tests::support::assert_true;

  const DiagnosticsCommand command{
      .command_id = std::string("diag-cmd-assembler-001"),
      .command_name = std::string("health.snapshot"),
      .args = {std::string("--summary")},
      .request_scope = std::string("runtime"),
      .timeout_ms = 3000,
      .actor_ref = std::string("ops-user"),
  };

  const auto execution = CommandExecutionResult::success(
      std::string("command://diagnostics/health.snapshot/v1"),
      std::string("diagnostics executor health snapshot"),
      {std::string("health://diagnostics/health.snapshot"),
       std::string("logs://diagnostics/health.snapshot")},
      std::string("2026-04-07T20:00:00Z"),
      17);
  assert_true(execution.is_valid(),
              "snapshot assembler test requires a structurally valid execution result");

  const auto evidence = EvidenceCollector{}.collect(command, execution);
  assert_true(evidence.is_valid(),
              "snapshot assembler test requires a structurally valid evidence bundle");

  SnapshotAssembler assembler;
  const auto first_snapshot = assembler.assemble(command, execution, evidence);
  const auto second_snapshot = assembler.assemble(command, execution, evidence);

  assert_true(first_snapshot.is_valid(),
              "snapshot assembler should emit a valid diagnostics snapshot from the executor and evidence bundle");
  assert_true(first_snapshot.snapshot_id != second_snapshot.snapshot_id,
              "snapshot assembler should generate a unique snapshot_id on each assembly call");
  assert_true(first_snapshot.snapshot_id.rfind("diag-snapshot-", 0) == 0,
              "snapshot assembler should keep the diagnostics snapshot id prefix stable");
  assert_true(first_snapshot.summary == execution.summary,
              "snapshot assembler should preserve the executor-produced summary");
  assert_true(first_snapshot.collected_at == execution.executed_at,
              "snapshot assembler should preserve the executor timestamp as the snapshot collected_at anchor");
  assert_true(first_snapshot.evidence_refs.size() == evidence.artifacts.size() + 4,
              "snapshot assembler should bind the four canonical evidence refs plus all collected artifacts");
  assert_true(contains_ref(first_snapshot.evidence_refs, evidence.logs_ref) &&
                  contains_ref(first_snapshot.evidence_refs, evidence.metrics_ref) &&
                  contains_ref(first_snapshot.evidence_refs, evidence.health_ref) &&
                  contains_ref(first_snapshot.evidence_refs, evidence.errors_ref),
              "snapshot assembler should keep all canonical evidence refs reachable from the snapshot");
}

}  // namespace

int main() {
  try {
    test_diagnostics_types_freeze_read_only_command_and_local_export_contract();
    test_diagnostics_types_reject_non_whitelisted_or_remote_unsafe_requests();
    test_snapshot_assembler_generates_unique_snapshot_ids_and_binds_evidence_refs();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}