#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace dasall::access {
class IAccessGateway;
}

namespace dasall::platform {
class IIPC;
}

namespace dasall::infra::watchdog {
class IWatchdogService;
}

namespace dasall::apps::daemon {

enum class DaemonStartupMode {
  DirectBind = 0,
  SocketActivated = 1,
};

enum class DaemonConfigSource {
  Defaults = 0,
  Profile = 1,
  DeploymentOverride = 2,
  RuntimeOverride = 3,
  ConfigFile = 4,
  CommandLine = 5,
};

struct DaemonConfigConflict {
  std::string key;
  DaemonConfigSource first_source = DaemonConfigSource::Defaults;
  DaemonConfigSource second_source = DaemonConfigSource::Defaults;
  std::string first_value;
  std::string second_value;

  [[nodiscard]] bool has_consistent_values() const {
    return !key.empty() && first_source != second_source &&
           !first_value.empty() && !second_value.empty();
  }
};

struct DaemonBootstrapConfig {
  std::string socket_path = "/tmp/dasall/control.sock";
  std::uint32_t listen_backlog = 32U;
  std::uint32_t max_payload_bytes = 1048576U;
  std::int32_t dispatch_timeout_ms = 5000;
  std::int32_t shutdown_grace_ms = 3000;
  std::int32_t receipt_ttl_sec = 3600;
  std::uint32_t accept_workers = 1U;
  std::uint32_t dispatch_workers = 4U;
  bool diag_enabled = false;
  bool override_enabled = false;
  bool watchdog_enabled = false;
  std::string log_format = "json";
  DaemonStartupMode startup_mode = DaemonStartupMode::DirectBind;

  [[nodiscard]] bool has_consistent_values() const {
    if (socket_path.empty() || !socket_path.starts_with('/')) {
      return false;
    }

    if (listen_backlog == 0U || max_payload_bytes == 0U) {
      return false;
    }

    if (dispatch_timeout_ms <= 0 || shutdown_grace_ms <= 0 ||
        receipt_ttl_sec <= 0) {
      return false;
    }

    if (accept_workers == 0U || dispatch_workers == 0U) {
      return false;
    }

    return !log_format.empty();
  }
};

struct DaemonProcessContext {
  DaemonBootstrapConfig bootstrap_config;
  std::string effective_profile_id;
  std::shared_ptr<dasall::platform::IIPC> ipc;
  std::shared_ptr<dasall::access::IAccessGateway> access_gateway;
  std::shared_ptr<dasall::infra::watchdog::IWatchdogService> watchdog_service;
  std::optional<std::string> config_revision;

  [[nodiscard]] bool has_consistent_values() const {
    return bootstrap_config.has_consistent_values() && !effective_profile_id.empty() &&
           ipc != nullptr && access_gateway != nullptr;
  }
};

}  // namespace dasall::apps::daemon