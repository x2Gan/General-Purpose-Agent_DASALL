#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "AccessGatewayFactory.h"
#include "DaemonConfigReloader.h"
#include "DaemonEntryConfigLoader.h"
#include "IAccessGateway.h"
#include "diagnostics/IDiagnosticsService.h"
#include "support/TestAssertions.h"

namespace {

namespace fs = std::filesystem;

class RecordingDiagnosticsService final
    : public dasall::infra::diagnostics::IDiagnosticsService {
 public:
  dasall::infra::diagnostics::DiagnosticsSnapshotResult execute(
      const dasall::infra::diagnostics::DiagnosticsCommand& command) override {
    ++execute_calls;

    dasall::infra::diagnostics::DiagnosticsSnapshot snapshot;
    snapshot.snapshot_id = command.command_id;
    snapshot.summary = "diag_enabled_after_reload";

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

[[nodiscard]] fs::path repository_root() {
  return fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

[[nodiscard]] fs::path make_temp_directory() {
  const auto unique_suffix = std::to_string(
      std::chrono::high_resolution_clock::now().time_since_epoch().count());
  const auto temp_dir = fs::temp_directory_path() /
                        "dasall-daemon-hot-reload-integration" /
                        unique_suffix;
  fs::create_directories(temp_dir);
  return temp_dir;
}

void write_file(const fs::path& file_path, const std::string& content) {
  std::ofstream stream(file_path);
  stream << content;
}

[[nodiscard]] dasall::access::InboundPacket make_diag_packet() {
  dasall::access::InboundPacket packet;
  packet.packet_id = "diag";
  packet.entry_type = "daemon";
  packet.protocol_kind = "ipc_uds";
  packet.peer_ref = "local_trusted:1000";
  packet.payload = "command_name=health.snapshot";
  return packet;
}

void hot_reload_consumes_fresh_snapshot_and_updates_diag_gate() {
  using dasall::access::AccessDisposition;
  using dasall::access::DaemonAccessPipelineOptions;
  using dasall::apps::daemon::DaemonConfigReloader;
  using dasall::apps::daemon::DaemonEntryConfigLoader;
  using dasall::apps::daemon::DaemonEntryConfigLoadRequest;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto temp_root = make_temp_directory();
  const auto config_path = temp_root / "daemon-reload.yaml";
  write_file(config_path,
             "daemon:\n"
             "  socket_path: /tmp/dasall/reload.sock\n"
             "  diag_enabled: false\n"
             "  log_format: json\n");

  const DaemonEntryConfigLoader loader;
  const DaemonEntryConfigLoadRequest request{
      .profiles_root = repository_root() / "profiles",
      .requested_profile_id = "desktop_full",
      .deployment_config_path = config_path,
      .socket_path_override = std::nullopt,
  };
  const auto initial = loader.load(request);
  assert_true(initial.ok() && initial.entry_config.has_value(),
              "hot reload integration should load the initial entry config");

  std::vector<std::string> audited_keys;
  std::string audited_reason;
  DaemonConfigReloader reloader(
      initial.entry_config->bootstrap_config,
      [&audited_keys, &audited_reason](const std::vector<std::string>& keys,
                                       const std::string& reason) {
        audited_keys = keys;
        audited_reason = reason;
      });

  auto diagnostics_state = std::make_shared<std::atomic_bool>(
      initial.entry_config->bootstrap_config.diag_enabled);
  auto diagnostics_service = std::make_shared<RecordingDiagnosticsService>();

  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.daemon_profile_id = initial.entry_config->effective_profile_id;
  options.daemon_diagnostics_enabled = diagnostics_state->load();
  options.daemon_diagnostics_enabled_state = diagnostics_state;
  options.diagnostics_service = diagnostics_service;
  options.runtime_dispatch_backend = [](const auto&) {
    return dasall::access::RuntimeDispatchResult{};
  };

  auto gateway = dasall::access::create_daemon_access_gateway(std::move(options));
  assert_true(gateway != nullptr && gateway->init(),
              "hot reload integration should initialize a daemon access gateway");

  const auto denied = gateway->submit(make_diag_packet());
  assert_equal(static_cast<int>(AccessDisposition::Rejected),
               static_cast<int>(denied.disposition),
               "diagnostics command should be rejected before diag_enabled is reloaded");
  assert_true(denied.error_ref.has_value() && *denied.error_ref == "diag_disabled",
              "diagnostics command should expose diag_disabled before reload");
  assert_equal(0, diagnostics_service->execute_calls,
               "diagnostics service should stay unused before reload enables diag gate");

  write_file(config_path,
             "daemon:\n"
             "  socket_path: /tmp/dasall/reload.sock\n"
             "  diag_enabled: true\n"
             "  log_format: text\n");
  const auto allowlisted_candidate = loader.load(request);
  assert_true(allowlisted_candidate.ok() && allowlisted_candidate.entry_config.has_value(),
              "hot reload integration should load a fresh allowlisted candidate from config file");

  const auto allowlisted_result = reloader.apply_reload_snapshot(
      allowlisted_candidate.entry_config->bootstrap_config);
  assert_true(allowlisted_result.ok(),
              "hot reload integration should apply allowlisted reload keys from a fresh candidate");
  diagnostics_state->store(reloader.active_snapshot().diag_enabled);

  const auto accepted = gateway->submit(make_diag_packet());
  assert_equal(static_cast<int>(AccessDisposition::Completed),
               static_cast<int>(accepted.disposition),
               "diagnostics command should succeed after diag_enabled is reloaded from config file");
  assert_true(accepted.publish_envelope.has_value() &&
                  accepted.publish_envelope->payload.find("diag_enabled_after_reload") !=
                      std::string::npos,
              "diagnostics command should expose the diagnostics service payload after reload");
  assert_equal(1, diagnostics_service->execute_calls,
               "diagnostics service should be invoked exactly once after reload enables diag gate");

  write_file(config_path,
             "daemon:\n"
             "  socket_path: /tmp/dasall/reload-v2.sock\n"
             "  diag_enabled: true\n"
             "  log_format: text\n");
  const auto restart_only_candidate = loader.load(request);
  assert_true(restart_only_candidate.ok() && restart_only_candidate.entry_config.has_value(),
              "hot reload integration should still load fresh candidates before restart-only validation");

  const auto rejected_result = reloader.apply_reload_snapshot(
      restart_only_candidate.entry_config->bootstrap_config);
  assert_true(!rejected_result.ok(),
              "hot reload integration should reject restart-only socket_path changes from a fresh candidate");
  assert_equal(static_cast<std::size_t>(1), rejected_result.rejected_keys.size(),
               "restart-only reload rejection should surface one rejected key");
  assert_equal(std::string("daemon.socket_path"), rejected_result.rejected_keys.front(),
               "restart-only reload rejection should preserve socket_path key");
  assert_equal(std::string("reload_rejected_restart_only_keys"), audited_reason,
               "restart-only reload rejection should preserve the stable audit reason");
  assert_equal(std::string("daemon.socket_path"), audited_keys.front(),
               "restart-only reload rejection should preserve the stable audit key");

  diagnostics_state->store(reloader.active_snapshot().diag_enabled);
  const auto still_accepted = gateway->submit(make_diag_packet());
  assert_equal(static_cast<int>(AccessDisposition::Completed),
               static_cast<int>(still_accepted.disposition),
               "restart-only rejection should preserve the last-known-good diag gate state");

  fs::remove_all(temp_root);
}

}  // namespace

int main() {
  try {
    hot_reload_consumes_fresh_snapshot_and_updates_diag_gate();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonHotReloadIntegrationTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}