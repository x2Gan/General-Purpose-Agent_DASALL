#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "../telemetry/RuntimeEventBus.h"

namespace dasall::runtime {

struct BackgroundMaintenanceTick {
  std::uint64_t tick_sequence = 0U;
  bool system_idle = true;
  bool checkpoint_cleanup_due = false;
  bool session_expiry_due = false;
  bool health_probe_due = false;
  bool profile_refresh_due = false;
  bool telemetry_flush_due = false;
  std::string detail;
  std::int64_t timestamp_ms = 0;

  [[nodiscard]] bool has_due_work() const {
    return checkpoint_cleanup_due || session_expiry_due || health_probe_due ||
           profile_refresh_due || telemetry_flush_due;
  }
};

struct BackgroundMaintenanceHookOptions {
  std::function<std::int64_t()> now_ms;
  std::string event_name_prefix = "runtime.maintenance";
};

class BackgroundMaintenanceHooks final {
 public:
  explicit BackgroundMaintenanceHooks(
      std::shared_ptr<RuntimeEventBus> event_bus,
      BackgroundMaintenanceHookOptions options = {});

  [[nodiscard]] RuntimeEventPublishResult publish_idle_tick(
      const BackgroundMaintenanceTick& tick);

 private:
  [[nodiscard]] static std::int64_t default_now_ms();
  [[nodiscard]] RuntimeEventEnvelope make_idle_tick_event(
      const BackgroundMaintenanceTick& tick) const;

  std::shared_ptr<RuntimeEventBus> event_bus_;
  BackgroundMaintenanceHookOptions options_{};
};

}  // namespace dasall::runtime