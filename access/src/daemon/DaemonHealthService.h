#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "daemon/DaemonHealthTypes.h"

namespace dasall::access::daemon {

struct DaemonHealthInput {
  bool lifecycle_ready = false;
  bool listener_ready = false;
  bool gateway_ready = false;
  bool bridge_reachable = false;
  bool diagnostics_enabled = false;
  std::vector<std::string> degraded_reasons;
};

struct DaemonHealthSnapshot {
  DaemonReadinessSnapshot readiness;
  DaemonPingSummary ping;
};

class DaemonHealthService {
 public:
  DaemonHealthService(std::string daemon_version,
                      std::string schema_version,
                      std::string profile_id);

  [[nodiscard]] DaemonHealthSnapshot snapshot(
      const DaemonHealthInput& input,
      std::string_view request_id) const;

 private:
  [[nodiscard]] static DaemonReadinessState resolve_readiness_state(
      const DaemonHealthInput& input);

  std::string daemon_version_;
  std::string schema_version_;
  std::string profile_id_;
};

}  // namespace dasall::access::daemon
