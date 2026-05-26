#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "ITimer.h"

namespace dasall::platform::linux {

class PosixTimerProvider final : public ITimer {
 public:
  PosixTimerProvider() = default;
  ~PosixTimerProvider() override;

  PlatformResult<TimerHandle> start_once(const TimerSpec& spec,
                                         TimerCallback callback) override;
  PlatformResult<TimerHandle> start_periodic(const TimerSpec& spec,
                                             TimerCallback callback) override;
  PlatformResult<TimerCancelResult> cancel(const TimerHandle& handle) override;

 private:
  struct TimerState {
    TimerMode mode = TimerMode::OneShot;
    bool cancelled = false;
    bool completed = false;
    TimerDriftStats drift_stats;
    std::mutex state_mutex;
    std::condition_variable cancel_cv;
    std::thread worker;
  };

  [[nodiscard]] PlatformResult<TimerHandle> start(const TimerSpec& spec,
                                                   TimerCallback callback,
                                                   TimerMode expected_mode);
  [[nodiscard]] PlatformError make_error(PlatformErrorCode code,
                                         PlatformErrorCategory category,
                                         std::string detail) const;

  mutable std::mutex mutex_;
  std::uint64_t next_id_ = 1;
  std::unordered_map<std::uint64_t, std::shared_ptr<TimerState>> timers_;
};

}  // namespace dasall::platform::linux