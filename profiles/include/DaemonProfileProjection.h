#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "IProfileCatalog.h"
#include "ProfileError.h"

namespace dasall::profiles {

inline constexpr std::string_view kDaemonSocketPathKey = "daemon.socket_path";
inline constexpr std::string_view kDaemonListenBacklogKey = "daemon.listen_backlog";
inline constexpr std::string_view kDaemonDispatchTimeoutKey = "daemon.dispatch_timeout_ms";
inline constexpr std::string_view kDaemonDiagEnabledKey = "daemon.diag.enabled";
inline constexpr std::string_view kDaemonWatchdogEnabledKey = "daemon.watchdog.enabled";

struct DaemonProfileSettings {
  std::string effective_profile_id;
  std::string socket_path = "/tmp/dasall/control.sock";
  std::uint32_t listen_backlog = 32U;
  std::int32_t dispatch_timeout_ms = 5000;
  bool diag_enabled = false;
  bool watchdog_enabled = false;
  std::vector<std::string> defaulted_keys;

  [[nodiscard]] bool has_consistent_values() const {
    return !effective_profile_id.empty() && !socket_path.empty() &&
           socket_path.starts_with('/') && listen_backlog > 0U &&
           dispatch_timeout_ms > 0;
  }

  [[nodiscard]] bool used_default_for(std::string_view key) const {
    return std::find(defaulted_keys.begin(), defaulted_keys.end(), key) !=
           defaulted_keys.end();
  }
};

struct DaemonProfileProjectionRequest {
  std::string profile_id;

  [[nodiscard]] bool has_consistent_values() const {
    return !profile_id.empty();
  }
};

struct DaemonProfileProjectionResult {
  std::optional<DaemonProfileSettings> settings;
  std::optional<ProfileErrorCode> error_code;

  [[nodiscard]] bool ok() const {
    return settings.has_value() && !error_code.has_value();
  }

  [[nodiscard]] bool has_consistent_values() const {
    if (!settings.has_value()) {
      return error_code.has_value();
    }

    return !error_code.has_value() && settings->has_consistent_values();
  }
};

class DaemonProfileProjection {
 public:
  explicit DaemonProfileProjection(const IProfileCatalog& catalog);

  [[nodiscard]] DaemonProfileProjectionResult load(
      const DaemonProfileProjectionRequest& request) const;

 private:
  const IProfileCatalog& catalog_;
};

}  // namespace dasall::profiles