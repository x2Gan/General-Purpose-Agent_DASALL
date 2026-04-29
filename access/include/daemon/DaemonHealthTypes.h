#pragma once

#include <string>
#include <vector>

namespace dasall::access::daemon {

enum class DaemonReadinessState {
  NotReady = 0,
  Ready = 1,
  Degraded = 2,
};

[[nodiscard]] inline std::string to_readiness_string(
    const DaemonReadinessState state) {
  switch (state) {
    case DaemonReadinessState::Ready:
      return "READY";
    case DaemonReadinessState::Degraded:
      return "DEGRADED";
    case DaemonReadinessState::NotReady:
    default:
      return "NOT_READY";
  }
}

struct DaemonReadinessSnapshot {
  DaemonReadinessState state = DaemonReadinessState::NotReady;
  bool lifecycle_ready = false;
  bool listener_ready = false;
  bool gateway_ready = false;
  bool bridge_reachable = false;
  bool diagnostics_enabled = false;
  std::vector<std::string> degraded_reasons;
};

struct DaemonPingSummary {
  std::string daemon_version;
  std::string schema_version;
  std::string profile_id;
  std::string request_id;
  std::string readiness_summary;
};

}  // namespace dasall::access::daemon
