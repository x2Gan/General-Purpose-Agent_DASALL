#include "DaemonHealthService.h"

#include <utility>

namespace dasall::access::daemon {

DaemonHealthService::DaemonHealthService(std::string daemon_version,
                                         std::string schema_version,
                                         std::string profile_id)
    : daemon_version_(std::move(daemon_version)),
      schema_version_(std::move(schema_version)),
      profile_id_(std::move(profile_id)) {}

DaemonHealthSnapshot DaemonHealthService::snapshot(
    const DaemonHealthInput& input,
    const std::string_view request_id) const {
  DaemonHealthSnapshot output;
  output.readiness.state = resolve_readiness_state(input);
  output.readiness.lifecycle_ready = input.lifecycle_ready;
  output.readiness.listener_ready = input.listener_ready;
  output.readiness.gateway_ready = input.gateway_ready;
  output.readiness.bridge_reachable = input.bridge_reachable;
  output.readiness.diagnostics_enabled = input.diagnostics_enabled;
  output.readiness.degraded_reasons = input.degraded_reasons;

  output.ping.daemon_version = daemon_version_;
  output.ping.schema_version = schema_version_;
  output.ping.profile_id = profile_id_;
  output.ping.request_id = std::string(request_id);
  output.ping.readiness_summary = to_readiness_string(output.readiness.state);
  return output;
}

DaemonReadinessState DaemonHealthService::resolve_readiness_state(
    const DaemonHealthInput& input) {
  if (!input.lifecycle_ready || !input.listener_ready || !input.gateway_ready ||
      !input.bridge_reachable) {
    return DaemonReadinessState::NotReady;
  }

  if (!input.degraded_reasons.empty()) {
    return DaemonReadinessState::Degraded;
  }

  return DaemonReadinessState::Ready;
}

}  // namespace dasall::access::daemon
