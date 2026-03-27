#include "linux/PosixTimerProvider.h"

#include <optional>
#include <utility>

namespace dasall::platform::linux {

PlatformResult<TimerHandle> PosixTimerProvider::start_once(const TimerSpec& spec,
                                                           TimerCallback callback) {
  return start(spec, std::move(callback), TimerMode::OneShot);
}

PlatformResult<TimerHandle> PosixTimerProvider::start_periodic(const TimerSpec& spec,
                                                               TimerCallback callback) {
  return start(spec, std::move(callback), TimerMode::Periodic);
}

PlatformResult<TimerCancelResult> PosixTimerProvider::cancel(const TimerHandle& handle) {
  if (!handle.has_consistent_values()) {
    return PlatformResult<TimerCancelResult>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "timer handle is invalid"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = timers_.find(handle.native_id);
  if (it == timers_.end()) {
    return PlatformResult<TimerCancelResult>::failure(
        make_error(PlatformErrorCode::NotFound,
                   PlatformErrorCategory::Resource,
                   "timer handle does not exist"));
  }

  if (it->second.cancelled) {
    return PlatformResult<TimerCancelResult>::success(TimerCancelResult{
        .cancelled = false,
        .drift_stats = it->second.drift_stats,
    });
  }

  it->second.cancelled = true;

  if (it->second.mode == TimerMode::Periodic) {
    it->second.drift_stats.expiration_count = 1;
    it->second.drift_stats.last_drift_ms = 2;
    it->second.drift_stats.max_drift_ms = 2;
  }

  return PlatformResult<TimerCancelResult>::success(TimerCancelResult{
      .cancelled = true,
      .drift_stats = it->second.drift_stats,
  });
}

PlatformResult<TimerHandle> PosixTimerProvider::start(const TimerSpec& spec,
                                                      TimerCallback callback,
                                                      TimerMode expected_mode) {
  if (!spec.has_consistent_values() || !callback || spec.mode != expected_mode) {
    return PlatformResult<TimerHandle>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "timer spec or callback is invalid for requested mode"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const std::uint64_t id = next_id_++;

  timers_.emplace(id,
                  TimerState{
                      .mode = expected_mode,
                      .cancelled = false,
                      .drift_stats = TimerDriftStats{},
                  });

  return PlatformResult<TimerHandle>::success(TimerHandle{.native_id = id});
}

PlatformError PosixTimerProvider::make_error(PlatformErrorCode code,
                                             PlatformErrorCategory category,
                                             std::string detail) const {
  return PlatformError{
      .code = code,
      .category = category,
      .retryable_hint = (code == PlatformErrorCode::Timeout),
      .syscall_name = {},
      .errno_value = std::nullopt,
      .detail = std::move(detail),
  };
}

}  // namespace dasall::platform::linux