#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include <unistd.h>

#include "AccessGatewayFactory.h"
#include "DaemonIntegrationHarness.h"
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

class RecordingDiagnosticsService final : public dasall::infra::diagnostics::IDiagnosticsService {
 public:
  dasall::infra::diagnostics::DiagnosticsSnapshotResult execute(
      const dasall::infra::diagnostics::DiagnosticsCommand& command) override {
    ++execute_calls;

    dasall::infra::diagnostics::DiagnosticsSnapshot snapshot;
    snapshot.snapshot_id = command.command_id;
    snapshot.summary = "diag_allowed_via_real_peer_identity";

    dasall::infra::diagnostics::DiagnosticsSnapshotResult result;
    result.ok = true;
    result.snapshot = std::move(snapshot);
    return result;
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

[[nodiscard]] std::string current_local_actor_ref() {
  return "local://uid/" + std::to_string(::getuid());
}

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

void diag_command_succeeds_via_real_uds_peer_identity_when_actor_is_allowlisted() {
  using dasall::access::DaemonAccessPipelineOptions;
  using dasall::tests::integration::access_support::DaemonIntegrationHarness;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto diag_service = std::make_shared<RecordingDiagnosticsService>();

  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {current_local_actor_ref()};
  options.daemon_diagnostics_enabled = true;
  options.diagnostics_service = diag_service;
  options.policy_backend_available = true;
  options.allow_submit = true;
  options.runtime_dispatch_backend = [](const auto&) {
    return dasall::access::RuntimeDispatchResult{};
  };

  DaemonIntegrationHarness harness(std::move(options));
  auto client = harness.make_client();
  const auto response = client.run_diagnostics("health.snapshot");

  assert_true(response.ok(),
              "real uds diag request should receive a parseable daemon response");
  assert_true(response.is_completed(),
              "allowlisted local peer should pass daemon diagnostics authorization");
  assert_true(!response.error_ref.has_value(),
              "successful diagnostics response should not expose an error_ref");
  assert_equal(1, diag_service->execute_calls,
               "real peer identity path should invoke diagnostics service exactly once");
}

void diag_command_is_rejected_via_real_uds_peer_identity_when_actor_is_not_allowlisted() {
  using dasall::tests::integration::access_support::DaemonIntegrationHarness;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto diag_service = std::make_shared<RecordingDiagnosticsService>();

  dasall::access::DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/999999"};
  options.daemon_diagnostics_enabled = true;
  options.diagnostics_service = diag_service;
  options.policy_backend_available = true;
  options.allow_submit = true;
  options.runtime_dispatch_backend = [](const auto&) {
    return dasall::access::RuntimeDispatchResult{};
  };

  DaemonIntegrationHarness harness(std::move(options));
  auto client = harness.make_client();
  const auto response = client.run_diagnostics("health.snapshot");

  assert_true(response.ok(),
              "real uds rejected diag request should still return a parseable daemon response");
  assert_true(!response.is_completed(),
              "non-allowlisted local peer should be rejected before diagnostics execution");
  assert_true(response.error_ref.has_value(),
              "rejected diagnostics request should expose a stable error_ref");
  assert_equal(0, diag_service->execute_calls,
               "rejected real peer identity path must not invoke diagnostics service");
}

}  // namespace

int main() {
  try {
    diag_command_is_denied_when_daemon_diag_gate_disabled();
    diag_command_succeeds_via_real_uds_peer_identity_when_actor_is_allowlisted();
    diag_command_is_rejected_via_real_uds_peer_identity_when_actor_is_not_allowlisted();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonDiagDenyIntegrationTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
