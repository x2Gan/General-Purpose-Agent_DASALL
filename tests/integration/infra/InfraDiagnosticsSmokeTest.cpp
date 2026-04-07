#include <exception>
#include <iostream>
#include <string>

#include "diagnostics/DiagnosticsErrors.h"
#include "diagnostics/DiagnosticsServiceFacade.h"
#include "diagnostics/IDiagnosticsService.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_diagnostics_smoke_execute_get_and_export_round_trip() {
  using dasall::infra::diagnostics::DiagnosticsCommand;
  using dasall::infra::diagnostics::DiagnosticsServiceFacade;
  using dasall::infra::diagnostics::ExportFormat;
  using dasall::infra::diagnostics::ExportTarget;
  using dasall::tests::support::assert_true;

  DiagnosticsServiceFacade service;
  assert_true(service.start(),
              "diagnostics smoke flow should start the facade before execute/get/export");

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
  assert_true(get_result.snapshot.summary == "diagnostics redacted health snapshot",
              "diagnostics smoke flow should keep the strict redaction summary stable across retrieval");
  assert_true(get_result.snapshot.command.actor_ref == "actor://redacted",
              "diagnostics smoke flow should retain only the redacted actor reference after execute/get round trip");

  const auto export_result = service.export_snapshot({
      .snapshot_id = execute_result.snapshot.snapshot_id,
      .target = ExportTarget::LocalFile,
      .format = ExportFormat::Json,
      .target_ref = std::string("local://diagnostics/snapshot-001.jsonl"),
  });
  assert_true(export_result.ok,
              "diagnostics smoke flow should export the retained snapshot through the minimal local export contract");
    assert_true(export_result.checksum.rfind("sha256:", 0) == 0,
          "diagnostics smoke flow should expose a stable sha256 checksum prefix for local JSON Lines exports");

    const auto remote_export = service.export_snapshot({
      .snapshot_id = execute_result.snapshot.snapshot_id,
      .target = ExportTarget::RemoteUpload,
      .format = ExportFormat::Json,
      .target_ref = std::string("https://diagnostics.example.test/upload"),
    });
    assert_true(!remote_export.ok && remote_export.references_only_contract_error_types(),
          "diagnostics smoke flow should reject remote export requests while remote export stays disabled by default");
    assert_true(remote_export.result_code ==
            dasall::infra::diagnostics::map_diagnostics_error_code(
              dasall::infra::diagnostics::DiagnosticsErrorCode::RemoteExportDisabled)
              .result_code,
          "diagnostics smoke flow should map disabled remote export requests to the frozen remote-disabled diagnostics error");
}

void test_diagnostics_smoke_rejects_non_whitelisted_command() {
  using dasall::infra::diagnostics::DiagnosticsCommand;
  using dasall::infra::diagnostics::DiagnosticsServiceFacade;
  using dasall::tests::support::assert_true;

  DiagnosticsServiceFacade service;
  assert_true(service.start(),
              "diagnostics smoke denial path should start the facade before execute");

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

void test_diagnostics_smoke_surfaces_executor_failures() {
  using dasall::infra::diagnostics::DiagnosticsCommand;
  using dasall::infra::diagnostics::DiagnosticsServiceFacade;
  using dasall::tests::support::assert_true;

  DiagnosticsServiceFacade service;
  assert_true(service.start(),
              "diagnostics smoke executor failure path should start the facade before execute");

  const auto execute_result = service.execute(DiagnosticsCommand{
      .command_id = std::string("003"),
      .command_name = std::string("queue.stats"),
      .args = {std::string("--queue=missing")},
      .request_scope = std::string("runtime"),
      .timeout_ms = 3000,
      .actor_ref = std::string("ops-user"),
  });

  assert_true(!execute_result.ok,
              "diagnostics smoke flow should surface structured executor failures for runtime-unavailable diagnostics resources");
  assert_true(execute_result.references_only_contract_error_types(),
              "executor failures should remain within contracts ResultCode/ErrorInfo types");
  assert_true(execute_result.decision.allowed,
              "executor failures should preserve the successful authorization decision rather than rewriting it as a deny");
}

}  // namespace

int main() {
  try {
    test_diagnostics_smoke_execute_get_and_export_round_trip();
    test_diagnostics_smoke_rejects_non_whitelisted_command();
    test_diagnostics_smoke_surfaces_executor_failures();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}