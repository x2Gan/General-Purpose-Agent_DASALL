#include "linux/PosixTimerProvider.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>
#include <vector>

namespace dasall::platform::linux {
namespace {

[[nodiscard]] std::chrono::milliseconds initial_delay_for(const TimerSpec& spec) {
  if (spec.initial_delay_ms > 0U) {
    return std::chrono::milliseconds(spec.initial_delay_ms);
  }
  if (spec.mode == TimerMode::Periodic && spec.interval_ms > 0U) {
    return std::chrono::milliseconds(spec.interval_ms);
  }
  return std::chrono::milliseconds::zero();
}

[[nodiscard]] std::uint32_t clamp_drift_ms(
    const std::chrono::steady_clock::duration drift) {
  const auto drift_ms = std::chrono::duration_cast<std::chrono::milliseconds>(drift).count();
  return static_cast<std::uint32_t>(std::max<std::int64_t>(0, drift_ms));
}

}  // namespace

PosixTimerProvider::~PosixTimerProvider() {
  std::vector<TimerHandle> handles;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    handles.reserve(timers_.size());
    for (const auto& entry : timers_) {
      handles.push_back(TimerHandle{.native_id = entry.first});
    }
  }

  for (const auto& handle : handles) {
    (void)cancel(handle);
  }
}

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

  std::shared_ptr<TimerState> state;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = timers_.find(handle.native_id);
    if (it == timers_.end()) {
      return PlatformResult<TimerCancelResult>::failure(
          make_error(PlatformErrorCode::NotFound,
                     PlatformErrorCategory::Resource,
                     "timer handle does not exist"));
    }

    state = it->second;
  }

  bool cancelled = false;
  std::thread worker;
  {
    std::lock_guard<std::mutex> state_lock(state->state_mutex);
    if (!state->cancelled && !state->completed) {
      state->cancelled = true;
      cancelled = true;
    }
    state->cancel_cv.notify_all();
    worker.swap(state->worker);
  }

  if (worker.joinable()) {
    if (worker.get_id() == std::this_thread::get_id()) {
      worker.detach();
    } else {
      worker.join();
    }
  }

  TimerDriftStats drift_stats;
  {
    const std::lock_guard<std::mutex> state_lock(state->state_mutex);
    drift_stats = state->drift_stats;
  }

  return PlatformResult<TimerCancelResult>::success(TimerCancelResult{
      .cancelled = cancelled,
      .drift_stats = drift_stats,
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

  auto state = std::make_shared<TimerState>();
  state->mode = expected_mode;

  const auto interval = std::chrono::milliseconds(spec.interval_ms);
  const auto first_delay = initial_delay_for(spec);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::uint64_t id = next_id_++;
    timers_.emplace(id, state);

    state->worker = std::thread([state, spec, callback = std::move(callback), interval, first_delay]() {
      auto next_fire = std::chrono::steady_clock::now() + first_delay;
      while (true) {
        std::unique_lock<std::mutex> lock(state->state_mutex);
        if (state->cancelled) {
          break;
        }

        if (state->cancel_cv.wait_until(lock, next_fire, [&state]() {
              return state->cancelled;
            })) {
          break;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto drift_ms = clamp_drift_ms(now - next_fire);
        state->drift_stats.expiration_count += 1U;
        state->drift_stats.last_drift_ms = drift_ms;
        state->drift_stats.max_drift_ms =
            std::max(state->drift_stats.max_drift_ms, drift_ms);
        const auto drift_stats = state->drift_stats;
        if (spec.mode == TimerMode::OneShot) {
          state->completed = true;
        }
        lock.unlock();

        callback(drift_stats);

        if (spec.mode == TimerMode::OneShot) {
          break;
        }

        next_fire += interval;
      }
    });

    return PlatformResult<TimerHandle>::success(TimerHandle{.native_id = id});
  }
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