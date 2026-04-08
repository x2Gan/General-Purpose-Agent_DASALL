#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "ITimer.h"
#include "watchdog/DeadlineWheel.h"
#include "watchdog/HeartbeatIngestor.h"
#include "watchdog/HeartbeatRegistry.h"
#include "watchdog/WatchdogErrors.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

class RecordingTimer final : public dasall::platform::ITimer {
 public:
  dasall::platform::PlatformResult<dasall::platform::TimerHandle> start_once(
      const dasall::platform::TimerSpec& spec,
      dasall::platform::TimerCallback callback) override {
    last_one_shot_spec = spec;
    one_shot_callbacks += callback ? 1 : 0;
    return dasall::platform::PlatformResult<dasall::platform::TimerHandle>::success(
        dasall::platform::TimerHandle{.native_id = 77});
  }

  dasall::platform::PlatformResult<dasall::platform::TimerHandle> start_periodic(
      const dasall::platform::TimerSpec& spec,
      dasall::platform::TimerCallback callback) override {
    last_periodic_spec = spec;
    start_periodic_calls += 1;
    periodic_callbacks += callback ? 1 : 0;
    return dasall::platform::PlatformResult<dasall::platform::TimerHandle>::success(
        dasall::platform::TimerHandle{.native_id = 42});
  }

  dasall::platform::PlatformResult<dasall::platform::TimerCancelResult> cancel(
      const dasall::platform::TimerHandle& handle) override {
    cancelled_handle = handle.native_id;
    return dasall::platform::PlatformResult<dasall::platform::TimerCancelResult>::success(
        dasall::platform::TimerCancelResult{
            .cancelled = true,
            .drift_stats = {},
        });
  }

  std::optional<dasall::platform::TimerSpec> last_one_shot_spec;
  std::optional<dasall::platform::TimerSpec> last_periodic_spec;
  int start_periodic_calls = 0;
  int periodic_callbacks = 0;
  int one_shot_callbacks = 0;
  std::uint64_t cancelled_handle = 0;
};

[[nodiscard]] dasall::infra::watchdog::WatchedEntityDescriptor make_descriptor(
    std::string entity_id,
    std::string owner_module,
    std::uint32_t timeout_ms = 1500,
    std::uint32_t grace_ms = 250) {
  return dasall::infra::watchdog::WatchedEntityDescriptor{
      .entity_id = std::move(entity_id),
      .entity_type = std::string("thread"),
      .owner_module = std::move(owner_module),
      .criticality = dasall::infra::watchdog::WatchdogEntityCriticality::Critical,
      .timeout_ms = timeout_ms,
      .grace_ms = grace_ms,
  };
}

[[nodiscard]] dasall::infra::watchdog::HeartbeatSample make_sample(
    std::string entity_id,
    std::uint64_t seq_no,
    std::int64_t heartbeat_ts,
    std::int64_t deadline_ts) {
  return dasall::infra::watchdog::HeartbeatSample{
      .entity_id = std::move(entity_id),
      .heartbeat_ts = heartbeat_ts,
      .deadline_ts = deadline_ts,
      .seq_no = seq_no,
  };
}

void test_deadline_wheel_collects_only_due_candidates_from_registry_and_latest_samples() {
  using dasall::infra::watchdog::DeadlineWheel;
  using dasall::infra::watchdog::HeartbeatIngestor;
  using dasall::infra::watchdog::HeartbeatRegistry;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  HeartbeatRegistry registry;
  assert_true(registry.register_entity(make_descriptor("runtime.main_loop", "runtime")).ok,
              "DeadlineWheel tests require runtime.main_loop to be registered");
  assert_true(registry.register_entity(make_descriptor("services.worker", "services")).ok,
              "DeadlineWheel tests require services.worker to be registered");

  HeartbeatIngestor ingestor(&registry, 4U);
  assert_true(ingestor.ingest(make_sample("runtime.main_loop", 1, 1000, 1500)).ok,
              "DeadlineWheel should see an accepted latest sample for runtime.main_loop");
  assert_true(ingestor.ingest(make_sample("services.worker", 1, 1100, 3200)).ok,
              "DeadlineWheel should see an accepted latest sample for services.worker");

  DeadlineWheel wheel({}, &registry, &ingestor, nullptr);
  const auto result = wheel.tick_collect_due(2000);

  assert_true(result.ok && result.has_due_candidates() && result.due_candidates.size() == 1,
              "DeadlineWheel should collect only entities whose latest heartbeat deadline is already due");
  assert_equal(std::string("runtime.main_loop"),
               result.due_candidates.front().descriptor.entity_id,
               "DeadlineWheel should emit the overdue runtime.main_loop candidate first");
  assert_true(result.due_candidates.front().has_consistent_values() &&
                  result.due_candidates.front().overdue_by_ms == 500,
              "DeadlineWheel should preserve descriptor/sample linkage and overdue_by_ms for due candidates");
}

void test_deadline_wheel_scan_once_arms_monotonic_periodic_scheduler() {
  using dasall::infra::watchdog::DeadlineWheel;
  using dasall::infra::watchdog::HeartbeatIngestor;
  using dasall::infra::watchdog::HeartbeatRegistry;
  using dasall::infra::watchdog::WatchdogServiceConfig;
  using dasall::platform::TimerClockKind;
  using dasall::platform::TimerMode;
  using dasall::tests::support::assert_true;

  HeartbeatRegistry registry;
  assert_true(registry.register_entity(make_descriptor("runtime.scan_loop", "runtime")).ok,
              "DeadlineWheel scan_once tests require runtime.scan_loop to be registered");

  HeartbeatIngestor ingestor(&registry, 2U);
  assert_true(ingestor.ingest(make_sample("runtime.scan_loop", 1, 1000, 5000)).ok,
              "DeadlineWheel scan_once tests require one accepted sample before scheduling begins");

  WatchdogServiceConfig config;
  config.scan_interval_ms = 500;
  config.safe_mode_scan_interval_ms = 2000;

  auto timer = std::make_shared<RecordingTimer>();
  DeadlineWheel wheel(config, &registry, &ingestor, timer);
  const auto result = wheel.scan_once();

  assert_true(result.ok && result.scheduler_started && wheel.scheduler_armed(),
              "DeadlineWheel scan_once should arm the periodic scheduler on the first scan round");
  assert_true(timer->start_periodic_calls == 1 && timer->last_periodic_spec.has_value(),
              "DeadlineWheel should reach the platform ITimer::start_periodic entrypoint exactly once when the scheduler is first armed");
  assert_true(timer->last_periodic_spec->mode == TimerMode::Periodic &&
                  timer->last_periodic_spec->interval_ms == 500 &&
                  timer->last_periodic_spec->initial_delay_ms == 500 &&
                  timer->last_periodic_spec->clock_kind == TimerClockKind::Monotonic,
              "DeadlineWheel should freeze periodic scan scheduling onto the platform monotonic clock abstraction");
}

void test_deadline_wheel_marks_scan_gap_overdue_and_enters_safe_observe_mode() {
  using dasall::infra::watchdog::DeadlineWheel;
  using dasall::infra::watchdog::HeartbeatIngestor;
  using dasall::infra::watchdog::HeartbeatRegistry;
  using dasall::infra::watchdog::watchdog_error_code_name;
  using dasall::infra::watchdog::WatchdogErrorCode;
  using dasall::infra::watchdog::WatchdogServiceConfig;
  using dasall::tests::support::assert_true;

  HeartbeatRegistry registry;
  assert_true(registry.register_entity(make_descriptor("runtime.main_loop", "runtime")).ok,
              "DeadlineWheel overdue tests require a registered entity");

  HeartbeatIngestor ingestor(&registry, 2U);
  assert_true(ingestor.ingest(make_sample("runtime.main_loop", 1, 1000, 1500)).ok,
              "DeadlineWheel overdue tests require an accepted sample before scan gap evaluation");

  WatchdogServiceConfig config;
  config.scan_interval_ms = 500;
  config.safe_mode_scan_interval_ms = 2000;

  DeadlineWheel wheel(config, &registry, &ingestor, nullptr);
  assert_true(wheel.tick_collect_due(500).ok,
              "DeadlineWheel should accept the initial scan before overdue gap evaluation");

  const auto overdue = wheel.tick_collect_due(2801);

  assert_true(!overdue.ok && overdue.references_only_contract_error_types() &&
                  overdue.watchdog_code.has_value() &&
                  *overdue.watchdog_code == WatchdogErrorCode::ScanOverdue &&
                  overdue.error.has_value() &&
                  overdue.error->details.message.find(
                      std::string(watchdog_error_code_name(WatchdogErrorCode::ScanOverdue))) != std::string::npos &&
                  overdue.safe_observe_mode && wheel.safe_observe_mode() &&
                  overdue.scan_lag_ms > 0,
              "DeadlineWheel should surface INF_E_WATCHDOG_SCAN_OVERDUE and enter safe_observe_mode once the scan gap exceeds the frozen safe-mode interval");
}

}  // namespace

int main() {
  try {
    test_deadline_wheel_collects_only_due_candidates_from_registry_and_latest_samples();
    test_deadline_wheel_scan_once_arms_monotonic_periodic_scheduler();
    test_deadline_wheel_marks_scan_gap_overdue_and_enters_safe_observe_mode();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}