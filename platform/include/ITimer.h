#pragma once

#include <cstdint>
#include <functional>

#include "PlatformResult.h"

namespace dasall::platform {

enum class TimerMode {
  OneShot,
  Periodic,
};

enum class TimerClockKind {
  Monotonic,
  Boottime,
  Realtime,
};

struct TimerSpec {
  TimerMode mode = TimerMode::OneShot;
  std::uint32_t interval_ms = 0;
  std::uint32_t initial_delay_ms = 0;
  TimerClockKind clock_kind = TimerClockKind::Monotonic;

  [[nodiscard]] bool has_consistent_values() const {
    if (mode == TimerMode::Periodic && interval_ms == 0U) {
      return false;
    }

    return true;
  }
};

struct TimerHandle {
  std::uint64_t native_id = 0;

  [[nodiscard]] bool has_consistent_values() const {
    return native_id != 0;
  }
};

struct TimerDriftStats {
  std::uint64_t expiration_count = 0;
  std::uint32_t last_drift_ms = 0;
  std::uint32_t max_drift_ms = 0;

  [[nodiscard]] bool has_consistent_values() const {
    if (expiration_count == 0U && (last_drift_ms != 0U || max_drift_ms != 0U)) {
      return false;
    }

    if (max_drift_ms < last_drift_ms) {
      return false;
    }

    return true;
  }
};

struct TimerCancelResult {
  bool cancelled = false;
  TimerDriftStats drift_stats;

  [[nodiscard]] bool has_consistent_values() const {
    return drift_stats.has_consistent_values();
  }
};

using TimerCallback = std::function<void(const TimerDriftStats&)>;

class ITimer {
 public:
  virtual ~ITimer() = default;

  virtual PlatformResult<TimerHandle> start_once(const TimerSpec& spec,
                                                 TimerCallback callback) = 0;
  virtual PlatformResult<TimerHandle> start_periodic(const TimerSpec& spec,
                                                     TimerCallback callback) = 0;
  virtual PlatformResult<TimerCancelResult> cancel(const TimerHandle& handle) = 0;
};

}  // namespace dasall::platform