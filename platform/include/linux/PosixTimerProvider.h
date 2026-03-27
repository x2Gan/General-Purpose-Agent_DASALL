#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "ITimer.h"

namespace dasall::platform::linux {

class PosixTimerProvider final : public ITimer {
 public:
  PosixTimerProvider() = default;

  PlatformResult<TimerHandle> start_once(const TimerSpec& spec,
                                         TimerCallback callback) override;
  PlatformResult<TimerHandle> start_periodic(const TimerSpec& spec,
                                             TimerCallback callback) override;
  PlatformResult<TimerCancelResult> cancel(const TimerHandle& handle) override;

 private:
  struct TimerState {
    TimerMode mode = TimerMode::OneShot;
    bool cancelled = false;
    TimerDriftStats drift_stats;
  };

  [[nodiscard]] PlatformResult<TimerHandle> start(const TimerSpec& spec,
                                                   TimerCallback callback,
                                                   TimerMode expected_mode);
  [[nodiscard]] PlatformError make_error(PlatformErrorCode code,
                                         PlatformErrorCategory category,
                                         std::string detail) const;

  mutable std::mutex mutex_;
  std::uint64_t next_id_ = 1;
  std::unordered_map<std::uint64_t, TimerState> timers_;
};

}  // namespace dasall::platform::linux