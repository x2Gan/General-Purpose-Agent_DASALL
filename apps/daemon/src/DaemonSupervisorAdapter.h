#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "watchdog/IWatchdogService.h"

namespace dasall::apps::daemon {

struct DaemonSupervisorAdapterOptions {
  bool watchdog_enabled = false;
  std::string watchdog_entity_id = "daemon.main_loop";
  std::uint32_t watchdog_timeout_ms = 15000U;
  std::uint32_t watchdog_grace_ms = 2000U;
};

class DaemonSupervisorAdapter {
 public:
  explicit DaemonSupervisorAdapter(
      std::shared_ptr<dasall::infra::watchdog::IWatchdogService> watchdog_service = nullptr,
      DaemonSupervisorAdapterOptions options = {});

  [[nodiscard]] dasall::infra::watchdog::WatchdogOperationResult notify_ready();
  [[nodiscard]] dasall::infra::watchdog::WatchdogOperationResult notify_stopping();
  [[nodiscard]] dasall::infra::watchdog::WatchdogOperationResult tick_watchdog();

  [[nodiscard]] bool watchdog_active() const;

 private:
  [[nodiscard]] bool should_noop() const;
  [[nodiscard]] dasall::infra::watchdog::WatchedEntityDescriptor make_descriptor() const;
  [[nodiscard]] dasall::infra::watchdog::HeartbeatSample make_heartbeat_sample();

  std::shared_ptr<dasall::infra::watchdog::IWatchdogService> watchdog_service_;
  DaemonSupervisorAdapterOptions options_;
  bool watchdog_registered_ = false;
  std::uint64_t heartbeat_seq_no_ = 0;
};

}  // namespace dasall::apps::daemon