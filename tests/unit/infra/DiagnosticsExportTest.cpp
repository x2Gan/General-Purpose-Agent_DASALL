#include <exception>
#include <iostream>
#include <string>

#include "diagnostics/DiagnosticsErrors.h"
#include "diagnostics/ExportManager.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::infra::diagnostics::DiagnosticsSnapshot make_snapshot() {
  return dasall::infra::diagnostics::DiagnosticsSnapshot{
      .snapshot_id = std::string("diag-export-snapshot-001"),
      .command = dasall::infra::diagnostics::DiagnosticsCommand{
          .command_id = std::string("diag-export-command-001"),
          .command_name = std::string("health.snapshot"),
          .args = {std::string("--summary")},
          .request_scope = std::string("runtime"),
          .timeout_ms = 3000,
          .actor_ref = std::string("actor://redacted"),
      },
      .collected_at = std::string("2026-04-07T22:30:00Z"),
      .summary = std::string("diagnostics redacted health snapshot"),
      .evidence_refs = {std::string("logs://diagnostics/health.snapshot"),
                        std::string("metrics://diagnostics/health.snapshot"),
                        std::string("health://diagnostics/health.snapshot"),
                        std::string("errors://diagnostics/health.snapshot"),
                        std::string("command://diagnostics/health.snapshot")},
      .redaction_profile = dasall::infra::diagnostics::RedactionProfile::Strict,
      .exporter_hint = std::string("local_file"),
  };
}

[[nodiscard]] bool is_sha256_checksum(std::string_view checksum) {
  if (checksum.rfind("sha256:", 0) != 0 || checksum.size() != 71U) {
    return false;
  }

  return std::all_of(checksum.begin() + 7, checksum.end(), [](unsigned char character) {
    return std::isdigit(character) != 0 || (character >= 'a' && character <= 'f');
  });
}

void test_export_manager_exports_local_jsonl_snapshot_with_sha256_checksum() {
  using dasall::infra::diagnostics::ExportFormat;
  using dasall::infra::diagnostics::ExportManager;
  using dasall::infra::diagnostics::ExportTarget;
  using dasall::tests::support::assert_true;

  const auto result = ExportManager{}.export_snapshot(
      make_snapshot(),
      {.snapshot_id = std::string("diag-export-snapshot-001"),
       .target = ExportTarget::LocalFile,
       .format = ExportFormat::Json,
       .target_ref = std::string("local://diagnostics/diag-export-snapshot-001.jsonl")});

  assert_true(result.ok && result.target == ExportTarget::LocalFile &&
                  result.format == ExportFormat::Json && result.size_bytes > 0 &&
                  is_sha256_checksum(result.checksum),
              "ExportManager should export local diagnostics snapshots as UTF-8 JSON Lines and return a sha256 checksum");
}

void test_export_manager_rejects_remote_export_when_disabled_by_default() {
  using dasall::infra::diagnostics::DiagnosticsErrorCode;
  using dasall::infra::diagnostics::ExportFormat;
  using dasall::infra::diagnostics::ExportManager;
  using dasall::infra::diagnostics::ExportTarget;
  using dasall::infra::diagnostics::map_diagnostics_error_code;
  using dasall::tests::support::assert_true;

  const auto result = ExportManager{}.export_snapshot(
      make_snapshot(),
      {.snapshot_id = std::string("diag-export-snapshot-001"),
       .target = ExportTarget::RemoteUpload,
       .format = ExportFormat::Json,
       .target_ref = std::string("https://diagnostics.example.test/upload")});

  assert_true(!result.ok && result.references_only_contract_error_types(),
              "ExportManager should reject remote export requests before attempting any upload when remote export is disabled");
  assert_true(result.result_code ==
                  map_diagnostics_error_code(DiagnosticsErrorCode::RemoteExportDisabled).result_code,
              "ExportManager should map disabled remote export requests to INF_E_DIAG_REMOTE_EXPORT_DISABLED");
}

void test_export_manager_rejects_unsupported_formats_and_invalid_local_targets() {
  using dasall::infra::diagnostics::DiagnosticsErrorCode;
  using dasall::infra::diagnostics::ExportFormat;
  using dasall::infra::diagnostics::ExportManager;
  using dasall::infra::diagnostics::ExportTarget;
  using dasall::infra::diagnostics::map_diagnostics_error_code;
  using dasall::tests::support::assert_true;

  const auto unsupported_format = ExportManager{}.export_snapshot(
      make_snapshot(),
      {.snapshot_id = std::string("diag-export-snapshot-001"),
       .target = ExportTarget::LocalFile,
       .format = ExportFormat::TextArchive,
       .target_ref = std::string("local://diagnostics/diag-export-snapshot-001.jsonl")});
  assert_true(!unsupported_format.ok && unsupported_format.references_only_contract_error_types(),
              "ExportManager should reject TextArchive until diagnostics export format support expands beyond the frozen JSON Lines contract");
  assert_true(unsupported_format.result_code ==
                  map_diagnostics_error_code(DiagnosticsErrorCode::ExportFail).result_code,
              "Unsupported local export formats should map to INF_E_DIAG_EXPORT_FAIL");

  const auto invalid_target = ExportManager{}.export_snapshot(
      make_snapshot(),
      {.snapshot_id = std::string("diag-export-snapshot-001"),
       .target = ExportTarget::LocalFile,
       .format = ExportFormat::Json,
       .target_ref = std::string("local://diagnostics/../diag-export-snapshot-001.jsonl")});
  assert_true(!invalid_target.ok && invalid_target.references_only_contract_error_types(),
              "ExportManager should reject local target refs that escape the frozen local diagnostics namespace");
  assert_true(invalid_target.result_code ==
                  map_diagnostics_error_code(DiagnosticsErrorCode::ExportFail).result_code,
              "Invalid local target refs should map to INF_E_DIAG_EXPORT_FAIL");
}

}  // namespace

int main() {
  try {
    test_export_manager_exports_local_jsonl_snapshot_with_sha256_checksum();
    test_export_manager_rejects_remote_export_when_disabled_by_default();
    test_export_manager_rejects_unsupported_formats_and_invalid_local_targets();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}