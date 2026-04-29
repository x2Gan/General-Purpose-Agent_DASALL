#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "daemon/DaemonDiagnosticsHandler.h"
#include "diagnostics/IDiagnosticsService.h"
#include "support/TestAssertions.h"

namespace {

class StubDiagnosticsService final : public dasall::infra::diagnostics::IDiagnosticsService {
 public:
  dasall::infra::diagnostics::DiagnosticsSnapshotResult execute(
      const dasall::infra::diagnostics::DiagnosticsCommand& command) override {
    last_command_name = command.command_name;
    return dasall::infra::diagnostics::DiagnosticsSnapshotResult::success(
        dasall::infra::diagnostics::DiagnosticsSnapshot{
            .snapshot_id = "snapshot-020",
            .command = command,
            .collected_at = "2026-04-29T00:00:00Z",
            .summary = "diag summary",
            .evidence_refs = {"logs://diag"},
            .redaction_profile = dasall::infra::diagnostics::RedactionProfile::Strict,
            .exporter_hint = "local-only",
        });
  }

  dasall::infra::diagnostics::DiagnosticsSnapshotResult get_snapshot(
      const dasall::infra::diagnostics::SnapshotQuery&) override {
    return dasall::infra::diagnostics::DiagnosticsSnapshotResult{};
  }

  dasall::infra::diagnostics::SnapshotExportResult export_snapshot(
      const dasall::infra::diagnostics::SnapshotExportRequest&) override {
    return dasall::infra::diagnostics::SnapshotExportResult{};
  }

  std::string last_command_name;
};

void diag_handler_denies_when_disabled() {
  using dasall::access::AccessDisposition;
  using dasall::access::daemon::DaemonDiagnosticsHandler;
  using dasall::tests::support::assert_equal;

  auto service = std::make_shared<StubDiagnosticsService>();
  DaemonDiagnosticsHandler handler(service, false);

  const auto result = handler.handle_diag("health.snapshot", "diag-020-disabled", "local://uid/1000");
  assert_equal(static_cast<int>(AccessDisposition::Rejected),
               static_cast<int>(result.disposition),
               "diagnostics should reject when diag gate is disabled");
  assert_equal(std::string("diag_disabled"), *result.error_ref,
               "disabled diagnostics should expose diag_disabled reason");
}

void diag_handler_denies_non_read_only_command() {
  using dasall::access::AccessDisposition;
  using dasall::access::daemon::DaemonDiagnosticsHandler;
  using dasall::tests::support::assert_equal;

  auto service = std::make_shared<StubDiagnosticsService>();
  DaemonDiagnosticsHandler handler(service, true);

  const auto result = handler.handle_diag("remote.export", "diag-020-invalid", "local://uid/1000");
  assert_equal(static_cast<int>(AccessDisposition::Rejected),
               static_cast<int>(result.disposition),
               "non-whitelisted diagnostics command should be rejected");
  assert_equal(std::string("diag_command_not_allowed"), *result.error_ref,
               "non-whitelisted diagnostics command should expose stable error ref");
}

void diag_handler_executes_whitelisted_command() {
  using dasall::access::AccessDisposition;
  using dasall::access::daemon::DaemonDiagnosticsHandler;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto service = std::make_shared<StubDiagnosticsService>();
  DaemonDiagnosticsHandler handler(service, true);

  const auto result = handler.handle_diag("health.snapshot", "diag-020-ok", "local://uid/1000");
  assert_equal(static_cast<int>(AccessDisposition::Completed),
               static_cast<int>(result.disposition),
               "whitelisted diagnostics command should complete");
  assert_true(result.publish_envelope.has_value(),
              "diagnostics completion should publish a response envelope");
  assert_equal(std::string("health.snapshot"), service->last_command_name,
               "diagnostics handler should forward whitelisted command to diagnostics service");
}

}  // namespace

int main() {
  try {
    diag_handler_denies_when_disabled();
    diag_handler_denies_non_read_only_command();
    diag_handler_executes_whitelisted_command();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonDiagnosticsHandlerTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
