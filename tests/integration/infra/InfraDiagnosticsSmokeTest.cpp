#include <exception>
#include <iostream>
#include <string>
#include <unordered_map>

#include "diagnostics/IDiagnosticsService.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

class InMemoryDiagnosticsService final : public dasall::infra::diagnostics::IDiagnosticsService {
 public:
  dasall::infra::diagnostics::DiagnosticsSnapshotResult execute(
      const dasall::infra::diagnostics::DiagnosticsCommand& command) override {
    using dasall::contracts::ResultCode;
    using dasall::infra::diagnostics::CommandDecision;
    using dasall::infra::diagnostics::DiagnosticsSnapshot;
    using dasall::infra::diagnostics::DiagnosticsSnapshotResult;
    using dasall::infra::diagnostics::RedactionProfile;

    if (!command.is_read_only_whitelisted()) {
      return DiagnosticsSnapshotResult::failure(
          ResultCode::ValidationFieldMissing,
          "diagnostics command must be read-only and whitelisted",
          "diagnostics.execute",
          "InMemoryDiagnosticsService",
          CommandDecision{
              .allowed = false,
              .reason_code = std::string("diag_command_denied"),
              .policy_ref = std::string("policy://diagnostics/readonly"),
              .denied_rule_id = std::string("readonly-only"),
          });
    }

    DiagnosticsSnapshot snapshot{
        .snapshot_id = std::string("snapshot-") + command.command_id,
        .command = command,
        .collected_at = std::string("2026-03-27T11:30:00Z"),
        .summary = std::string("collected read-only diagnostics evidence"),
        .evidence_refs = {"logs://diag/summary", "health://diag/health"},
        .redaction_profile = RedactionProfile::Strict,
        .exporter_hint = std::string("local_file"),
    };
    snapshots.emplace(snapshot.snapshot_id, snapshot);
    return DiagnosticsSnapshotResult::success(std::move(snapshot));
  }

  dasall::infra::diagnostics::DiagnosticsSnapshotResult get_snapshot(
      const dasall::infra::diagnostics::SnapshotQuery& query) override {
    using dasall::contracts::ResultCode;
    using dasall::infra::diagnostics::DiagnosticsSnapshotResult;

    const auto iterator = snapshots.find(query.snapshot_id);
    if (iterator == snapshots.end()) {
      return DiagnosticsSnapshotResult::failure(ResultCode::ValidationFieldMissing,
                                                "snapshot_id must resolve to a retained diagnostics snapshot",
                                                "diagnostics.get_snapshot",
                                                "InMemoryDiagnosticsService");
    }

    return DiagnosticsSnapshotResult::success(iterator->second);
  }

  dasall::infra::diagnostics::SnapshotExportResult export_snapshot(
      const dasall::infra::diagnostics::SnapshotExportRequest& request) override {
    using dasall::contracts::ResultCode;
    using dasall::infra::diagnostics::ExportTarget;
    using dasall::infra::diagnostics::SnapshotExportResult;

    if (!request.is_valid() || request.target != ExportTarget::LocalFile) {
      return SnapshotExportResult::failure(ResultCode::ValidationFieldMissing,
                                           "diagnostics export must stay local and fully specified in the minimal skeleton",
                                           "diagnostics.export",
                                           "InMemoryDiagnosticsService");
    }

    if (snapshots.find(request.snapshot_id) == snapshots.end()) {
      return SnapshotExportResult::failure(ResultCode::ValidationFieldMissing,
                                           "diagnostics export requires an existing retained snapshot",
                                           "diagnostics.export",
                                           "InMemoryDiagnosticsService");
    }

    return SnapshotExportResult::success(std::string("export-") + request.snapshot_id,
                                         request.target,
                                         request.format,
                                         256,
                                         "sha256:diag-export-001",
                                         "2026-03-27T11:31:00Z");
  }

 private:
  std::unordered_map<std::string, dasall::infra::diagnostics::DiagnosticsSnapshot> snapshots;
};

void test_diagnostics_smoke_execute_get_and_export_round_trip() {
  using dasall::infra::diagnostics::DiagnosticsCommand;
  using dasall::infra::diagnostics::ExportFormat;
  using dasall::infra::diagnostics::ExportTarget;
  using dasall::tests::support::assert_true;

  InMemoryDiagnosticsService service;
  const DiagnosticsCommand command{
      .command_id = std::string("001"),
      .command_name = std::string("health.snapshot"),
      .args = {},
      .request_scope = std::string("runtime"),
      .timeout_ms = 3000,
      .actor_ref = std::string("ops-user"),
  };

  const auto execute_result = service.execute(command);
  assert_true(execute_result.ok,
              "diagnostics smoke flow should execute a whitelisted read-only command");
  assert_true(execute_result.snapshot.is_valid(),
              "diagnostics smoke flow should produce a valid diagnostics snapshot");

  const auto get_result = service.get_snapshot({.snapshot_id = execute_result.snapshot.snapshot_id});
  assert_true(get_result.ok,
              "diagnostics smoke flow should read back the retained snapshot");
  assert_true(get_result.snapshot.summary == "collected read-only diagnostics evidence",
              "diagnostics smoke flow should keep the snapshot summary stable across retrieval");

  const auto export_result = service.export_snapshot({
      .snapshot_id = execute_result.snapshot.snapshot_id,
      .target = ExportTarget::LocalFile,
      .format = ExportFormat::Json,
      .target_ref = std::string("local://diagnostics/snapshot-001.json"),
  });
  assert_true(export_result.ok,
              "diagnostics smoke flow should export the retained snapshot through the minimal local export contract");
}

void test_diagnostics_smoke_rejects_non_whitelisted_command() {
  using dasall::infra::diagnostics::DiagnosticsCommand;
  using dasall::tests::support::assert_true;

  InMemoryDiagnosticsService service;
  const auto execute_result = service.execute(DiagnosticsCommand{
      .command_id = std::string("002"),
      .command_name = std::string("secret.dump"),
      .args = {},
      .request_scope = std::string("runtime"),
      .timeout_ms = 3000,
      .actor_ref = std::string("ops-user"),
  });

  assert_true(!execute_result.ok,
              "diagnostics smoke flow should reject commands outside the frozen read-only whitelist");
  assert_true(execute_result.references_only_contract_error_types(),
              "diagnostics denial should remain within contracts ResultCode/ErrorInfo types");
  assert_true(!execute_result.decision.allowed,
              "diagnostics denial should surface an explicit command decision");
}

}  // namespace

int main() {
  try {
    test_diagnostics_smoke_execute_get_and_export_round_trip();
    test_diagnostics_smoke_rejects_non_whitelisted_command();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}