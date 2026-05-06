#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ITimer.h"
#include "PlatformError.h"
#include "health/ProbeScheduler.h"
#include "support/TestAssertions.h"

namespace {

class RecordingTimer final : public dasall::platform::ITimer {
 public:
  dasall::platform::PlatformResult<dasall::platform::TimerHandle> start_once(
      const dasall::platform::TimerSpec& spec,
      dasall::platform::TimerCallback callback) override {
    one_shot_specs.push_back(spec);
    one_shot_callbacks += callback ? 1 : 0;
    return dasall::platform::PlatformResult<dasall::platform::TimerHandle>::success(
        dasall::platform::TimerHandle{.native_id = 91U});
  }

  dasall::platform::PlatformResult<dasall::platform::TimerHandle> start_periodic(
      const dasall::platform::TimerSpec& spec,
      dasall::platform::TimerCallback callback) override {
    periodic_specs.push_back(spec);
    periodic_callbacks += callback ? 1 : 0;
    return dasall::platform::PlatformResult<dasall::platform::TimerHandle>::success(
        dasall::platform::TimerHandle{.native_id =
                                          static_cast<std::uint64_t>(40U + periodic_specs.size())});
  }

  dasall::platform::PlatformResult<dasall::platform::TimerCancelResult> cancel(
      const dasall::platform::TimerHandle& handle) override {
    cancelled_handles.push_back(handle.native_id);
    return dasall::platform::PlatformResult<dasall::platform::TimerCancelResult>::success(
        dasall::platform::TimerCancelResult{
            .cancelled = true,
            .drift_stats = {},
        });
  }

  std::vector<dasall::platform::TimerSpec> one_shot_specs;
  std::vector<dasall::platform::TimerSpec> periodic_specs;
  std::vector<std::uint64_t> cancelled_handles;
  int one_shot_callbacks = 0;
  int periodic_callbacks = 0;
};

void test_probe_scheduler_arms_monotonic_timers_for_liveness_and_readiness() {
  using dasall::infra::HealthResolvedConfig;
  using dasall::infra::ProbeScheduler;
  using dasall::platform::TimerClockKind;
  using dasall::platform::TimerMode;
  using dasall::tests::support::assert_true;

  auto timer = std::make_shared<RecordingTimer>();
  HealthResolvedConfig config;
  config.liveness_interval_ms = 2500U;
  config.readiness_interval_ms = 7000U;

  ProbeScheduler scheduler(config, timer);
  const auto result = scheduler.start([](std::string_view) {});

  assert_true(result.status.ok && result.started && !result.fallback_active &&
                  scheduler.running() && timer->periodic_specs.size() == 2U &&
                  timer->periodic_callbacks == 2 &&
                  timer->periodic_specs.front().mode == TimerMode::Periodic &&
                  timer->periodic_specs.front().interval_ms == 2500U &&
                  timer->periodic_specs.front().initial_delay_ms == 2500U &&
                  timer->periodic_specs.front().clock_kind == TimerClockKind::Monotonic &&
                  timer->periodic_specs.back().interval_ms == 7000U &&
                  timer->periodic_specs.back().clock_kind == TimerClockKind::Monotonic,
              "ProbeScheduler should arm liveness/readiness cadence on the platform monotonic ITimer seam");
}

void test_probe_scheduler_tick_once_preserves_fallback_when_timer_is_missing() {
  using dasall::infra::ProbeScheduler;
  using dasall::tests::support::assert_true;

  std::vector<std::string> dispatched_groups;
  ProbeScheduler scheduler({}, nullptr);

  const auto started = scheduler.start(
      [&dispatched_groups](const std::string_view group) {
        dispatched_groups.push_back(std::string(group));
      });
  const auto tick_result = scheduler.tick_once();

  assert_true(started.status.ok && !started.started && started.fallback_active &&
                  scheduler.fallback_active() &&
                  scheduler.fallback_reason() == "health.probe_scheduler://timer-unavailable" &&
                  tick_result.status.ok && tick_result.triggered &&
                  tick_result.fallback_active &&
                  tick_result.dispatched_groups.size() == 2U &&
                  dispatched_groups == tick_result.dispatched_groups,
              "ProbeScheduler should fall back to synchronous tick_once dispatch when no ITimer provider is available");
}

void test_probe_scheduler_stop_cancels_active_timer_handles() {
  using dasall::infra::ProbeScheduler;
  using dasall::tests::support::assert_true;

  auto timer = std::make_shared<RecordingTimer>();
  ProbeScheduler scheduler({}, timer);

  const auto started = scheduler.start([](std::string_view) {});
  const auto stopped = scheduler.stop();

  assert_true(started.status.ok && started.started && stopped.status.ok &&
                  stopped.stopped && !stopped.fallback_active &&
                  !scheduler.running() && timer->cancelled_handles.size() == 2U,
              "ProbeScheduler should cancel both active cadence timers during stop");
}

}  // namespace

int main() {
  try {
    test_probe_scheduler_arms_monotonic_timers_for_liveness_and_readiness();
    test_probe_scheduler_tick_once_preserves_fallback_when_timer_is_missing();
    test_probe_scheduler_stop_cancels_active_timer_handles();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}