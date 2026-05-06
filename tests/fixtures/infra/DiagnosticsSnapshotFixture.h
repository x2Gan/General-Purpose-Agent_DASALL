#pragma once

#include <string>
#include <vector>

#include "diagnostics/DiagnosticsTypes.h"

namespace dasall::tests::fixtures::infra {

namespace diag = dasall::infra::diagnostics;

inline constexpr const char* kDiagnosticsRetainedSummary =
    "diagnostics redacted health snapshot";
inline constexpr const char* kDiagnosticsRetainedActorRef = "actor://redacted";
inline constexpr const char* kDiagnosticsExporterHint = "local_file";

[[nodiscard]] inline std::vector<std::string> retained_snapshot_evidence_refs() {
  return {
      "logs://diagnostics/health.snapshot",
      "metrics://diagnostics/health.snapshot",
      "health://diagnostics/health.snapshot",
      "errors://diagnostics/health.snapshot/none",
      "health://diagnostics/health.snapshot",
      "logs://diagnostics/health.snapshot",
      "command://diagnostics/health.snapshot/v1",
  };
}

[[nodiscard]] inline diag::DiagnosticsCommand make_execute_command_fixture(
    std::string command_id = "diag-fixture-cmd-001",
    std::string actor_ref = "ops-user") {
  return diag::DiagnosticsCommand{
      .command_id = std::move(command_id),
      .command_name = std::string("health.snapshot"),
      .args = {},
      .request_scope = std::string("runtime"),
      .timeout_ms = 3000,
      .actor_ref = std::move(actor_ref),
  };
}

[[nodiscard]] inline diag::DiagnosticsSnapshot make_retained_snapshot_fixture(
    std::string snapshot_id = "diag-retained-snapshot-fixture-001",
    std::string collected_at = "2026-04-07T20:00:00Z") {
  return diag::DiagnosticsSnapshot{
      .snapshot_id = std::move(snapshot_id),
      .command = diag::DiagnosticsCommand{
          .command_id = std::string("diag-fixture-cmd-001"),
          .command_name = std::string("health.snapshot"),
          .args = {},
          .request_scope = std::string("runtime"),
          .timeout_ms = 3000,
          .actor_ref = std::string(kDiagnosticsRetainedActorRef),
      },
      .collected_at = std::move(collected_at),
      .summary = std::string(kDiagnosticsRetainedSummary),
      .evidence_refs = retained_snapshot_evidence_refs(),
      .redaction_profile = diag::RedactionProfile::Strict,
      .exporter_hint = std::string(kDiagnosticsExporterHint),
  };
}

[[nodiscard]] inline diag::SnapshotQuery make_snapshot_query_fixture(
    std::string snapshot_id = "diag-retained-snapshot-fixture-001") {
  return diag::SnapshotQuery{.snapshot_id = std::move(snapshot_id)};
}

[[nodiscard]] inline diag::SnapshotExportRequest make_local_export_request_fixture(
    std::string snapshot_id = "diag-retained-snapshot-fixture-001") {
  return diag::SnapshotExportRequest{
      .snapshot_id = std::move(snapshot_id),
      .target = diag::ExportTarget::LocalFile,
      .format = diag::ExportFormat::Json,
      .target_ref = std::string("local://diagnostics/retained-snapshot.jsonl"),
  };
}

[[nodiscard]] inline bool matches_retained_snapshot_contract(
    const diag::DiagnosticsSnapshot& snapshot) {
  return snapshot.is_valid() &&
         snapshot.summary == kDiagnosticsRetainedSummary &&
         snapshot.command.actor_ref == kDiagnosticsRetainedActorRef &&
         snapshot.evidence_refs == retained_snapshot_evidence_refs() &&
         snapshot.redaction_profile == diag::RedactionProfile::Strict &&
         snapshot.exporter_hint == kDiagnosticsExporterHint;
}

}  // namespace dasall::tests::fixtures::infra