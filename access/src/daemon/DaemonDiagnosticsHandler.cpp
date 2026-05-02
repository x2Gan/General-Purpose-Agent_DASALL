#include "DaemonDiagnosticsHandler.h"

#include <string>

#include "diagnostics/DiagnosticsTypes.h"

namespace dasall::access::daemon {

namespace {

[[nodiscard]] RuntimeDispatchResult make_rejected_diag_result(
    const std::string& request_id,
    std::string error_ref,
    std::string protocol_status_hint) {
  RuntimeDispatchResult result;
  result.disposition = AccessDisposition::Rejected;
  result.error_ref = error_ref;

  PublishEnvelope envelope;
  envelope.request_id = request_id;
  envelope.result_id = request_id;
  envelope.protocol_kind = "ipc_uds";
  envelope.protocol_status_hint = std::move(protocol_status_hint);
  envelope.payload = *result.error_ref;
  result.publish_envelope = std::move(envelope);
  return result;
}

}  // namespace

DaemonDiagnosticsHandler::DaemonDiagnosticsHandler(
    std::shared_ptr<dasall::infra::diagnostics::IDiagnosticsService> diagnostics_service,
    const bool diagnostics_enabled,
    std::shared_ptr<std::atomic_bool> diagnostics_enabled_state)
    : diagnostics_service_(std::move(diagnostics_service)),
      diagnostics_enabled_(diagnostics_enabled),
      diagnostics_enabled_state_(std::move(diagnostics_enabled_state)) {}

bool DaemonDiagnosticsHandler::diagnostics_enabled() const {
  if (diagnostics_enabled_state_) {
    return diagnostics_enabled_state_->load();
  }

  return diagnostics_enabled_;
}

RuntimeDispatchResult DaemonDiagnosticsHandler::handle_diag(
    const std::string_view command_name,
    const std::string_view request_id,
    const std::string_view actor_ref) const {
  if (!diagnostics_enabled()) {
    return make_rejected_diag_result(std::string(request_id), "diag_disabled", "403");
  }

  if (!is_read_only_diag_command(command_name)) {
    return make_rejected_diag_result(
        std::string(request_id), "diag_command_not_allowed", "403");
  }

  if (!diagnostics_service_) {
    return make_rejected_diag_result(
        std::string(request_id), "diag_service_unavailable", "503");
  }

  dasall::infra::diagnostics::DiagnosticsCommand command;
  command.command_id = std::string(request_id);
  command.command_name = std::string(command_name);
  command.request_scope = "daemon.local_control_plane";
  command.timeout_ms = 1000U;
  command.actor_ref = std::string(actor_ref);

  const auto execute_result = diagnostics_service_->execute(command);
  if (!execute_result.ok) {
    return make_rejected_diag_result(
        std::string(request_id), "diag_execution_failed", "503");
  }

  RuntimeDispatchResult result;
  result.disposition = AccessDisposition::Completed;

  PublishEnvelope envelope;
  envelope.request_id = std::string(request_id);
  envelope.result_id = execute_result.snapshot.snapshot_id;
  envelope.protocol_kind = "ipc_uds";
  envelope.protocol_status_hint = "200";
  envelope.payload = execute_result.snapshot.summary;
  result.publish_envelope = std::move(envelope);
  return result;
}

bool DaemonDiagnosticsHandler::is_read_only_diag_command(
    const std::string_view command_name) {
  return dasall::infra::diagnostics::is_read_only_command_whitelisted(command_name);
}

}  // namespace dasall::access::daemon
