#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "AccessGatewayFactory.h"
#include "IAccessGateway.h"
#include "diagnostics/IDiagnosticsService.h"
#include "support/TestAssertions.h"

namespace {

class FailingIfCalledDiagnosticsService final : public dasall::infra::diagnostics::IDiagnosticsService {
 public:
  dasall::infra::diagnostics::DiagnosticsSnapshotResult execute(
      const dasall::infra::diagnostics::DiagnosticsCommand&) override {
    ++execute_calls;
    return dasall::infra::diagnostics::DiagnosticsSnapshotResult{};
  }

  dasall::infra::diagnostics::DiagnosticsSnapshotResult get_snapshot(
      const dasall::infra::diagnostics::SnapshotQuery&) override {
    return dasall::infra::diagnostics::DiagnosticsSnapshotResult{};
  }

  dasall::infra::diagnostics::SnapshotExportResult export_snapshot(
      const dasall::infra::diagnostics::SnapshotExportRequest&) override {
    return dasall::infra::diagnostics::SnapshotExportResult{};
  }

  int execute_calls = 0;
};

void diag_command_is_denied_when_daemon_diag_gate_disabled() {
  using dasall::access::AccessDisposition;
  using dasall::access::DaemonAccessPipelineOptions;
  using dasall::access::InboundPacket;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto diag_service = std::make_shared<FailingIfCalledDiagnosticsService>();

  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.daemon_diagnostics_enabled = false;
  options.diagnostics_service = diag_service;
  options.policy_backend_available = true;
  options.allow_submit = true;
  options.runtime_dispatch_backend = [](const auto&) {
    return dasall::access::RuntimeDispatchResult{};
  };

  auto gateway = dasall::access::create_daemon_access_gateway(std::move(options));
  assert_true(gateway != nullptr, "diagnostics deny integration should create daemon gateway");
  assert_true(gateway->init(), "diagnostics deny integration should initialize daemon gateway");

  InboundPacket packet;
  packet.packet_id = "diag";
  packet.entry_type = "daemon";
  packet.protocol_kind = "ipc_uds";
  packet.peer_ref = "local_trusted:1000";
  packet.payload = "command_name=health.snapshot";

  const auto result = gateway->submit(packet);
  assert_equal(static_cast<int>(AccessDisposition::Rejected),
               static_cast<int>(result.disposition),
               "diag command should reject when daemon diag gate is disabled");
  assert_true(result.error_ref.has_value() && *result.error_ref == "diag_disabled",
              "diag deny integration should preserve diag_disabled error");
  assert_equal(0, diag_service->execute_calls,
               "disabled diag path should not invoke diagnostics service");
}

}  // namespace

int main() {
  try {
    diag_command_is_denied_when_daemon_diag_gate_disabled();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonDiagDenyIntegrationTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
