#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ITimer.h"
#include "watchdog/DeadlineWheel.h"
#include "watchdog/HeartbeatIngestor.h"
#include "watchdog/HeartbeatRegistry.h"
#include "watchdog/TimeoutDecision.h"
#include "watchdog/TimeoutEventPublisher.h"
#include "watchdog/WatchdogErrors.h"
#include "support/TestAssertions.h"

namespace {

class FailingTimeoutEventSink final
    : public dasall::infra::watchdog::ITimeoutEventSink {
 public:
  dasall::infra::watchdog::TimeoutEventDispatchResult publish_timeout_event(
      const dasall::infra::watchdog::TimeoutEvent& event) override {
    events.push_back(event);
    return dasall::infra::watchdog::TimeoutEventDispatchResult::failure(
        dasall::contracts::ResultCode::ToolExecutionFailed,
        "simulated watchdog event sink failure",
        "watchdog.failure.publish",
        "WatchdogFailureInjectionTest");
  }

  std::vector<dasall::infra::watchdog::TimeoutEvent> events;
};

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
    std::uint32_t timeout_ms = 15000,
    std::uint32_t grace_ms = 500) {
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

[[nodiscard]] dasall::infra::watchdog::TimeoutDecision make_decision(
    dasall::infra::watchdog::WatchdogTimeoutLevel timeout_level,
    std::uint32_t consecutive_miss) {
  return dasall::infra::watchdog::TimeoutDecision{
      .entity_id = std::string("runtime.main_loop"),
      .timeout_level = timeout_level,
      .consecutive_miss = consecutive_miss,
      .reason_code = dasall::contracts::ResultCode::ProviderTimeout,
      .evidence_ref = std::string("watchdog://timeout/runtime.main_loop"),
  };
}

void test_watchdog_failure_injection_buffers_publish_failures_locally() {
  using dasall::infra::watchdog::TimeoutEventPublisher;
  using dasall::infra::watchdog::WatchdogErrorCode;
  using dasall::infra::watchdog::WatchdogServiceConfig;
  using dasall::infra::watchdog::WatchdogTimeoutLevel;
  using dasall::tests::support::assert_true;

  auto sink = std::make_shared<FailingTimeoutEventSink>();
  WatchdogServiceConfig config;
  config.event_queue_size = 1;

  TimeoutEventPublisher publisher(sink, config);
  const auto result = publisher.publish_timeout(
      make_decision(WatchdogTimeoutLevel::Critical, 3));
  const auto status = publisher.status();

  assert_true(!result.emitted && result.buffered && result.is_valid() &&
                  result.watchdog_code.has_value() &&
                  *result.watchdog_code == WatchdogErrorCode::EventPublishFail,
              "WatchdogFailureInjectionTest should buffer the timeout event locally when the event sink rejects a critical publish request");
  assert_true(status.is_valid() && status.publish_fail_total == 1 &&
                  status.buffered_event_count == 1 && status.degraded,
              "WatchdogFailureInjectionTest should keep publish failures observable in publisher status instead of silently dropping the timeout event");
}

void test_watchdog_failure_injection_enters_safe_observe_mode_after_scan_lag() {
  using dasall::infra::watchdog::DeadlineWheel;
  using dasall::infra::watchdog::HeartbeatIngestor;
  using dasall::infra::watchdog::HeartbeatRegistry;
  using dasall::infra::watchdog::WatchdogErrorCode;
  using dasall::infra::watchdog::WatchdogServiceConfig;
  using dasall::tests::support::assert_true;

  HeartbeatRegistry registry;
  assert_true(registry.register_entity(make_descriptor("runtime.main_loop", "runtime")).ok,
              "WatchdogFailureInjectionTest requires a registered entity before scan lag injection is exercised");

  HeartbeatIngestor ingestor(&registry, 2U);
  assert_true(ingestor.ingest(make_sample("runtime.main_loop", 1, 1000, 1500)).ok,
              "WatchdogFailureInjectionTest requires one accepted sample before overdue scan gaps are evaluated");

  WatchdogServiceConfig config;
  config.scan_interval_ms = 500;
  config.safe_mode_scan_interval_ms = 2000;

  auto timer = std::make_shared<RecordingTimer>();
  DeadlineWheel wheel(config, &registry, &ingestor, timer);
  assert_true(wheel.tick_collect_due(500).ok,
              "WatchdogFailureInjectionTest requires one successful scan before the overdue gap is injected");

  const auto overdue = wheel.tick_collect_due(2801);

  assert_true(!overdue.ok && overdue.references_only_contract_error_types() &&
                  overdue.watchdog_code.has_value() &&
                  *overdue.watchdog_code == WatchdogErrorCode::ScanOverdue &&
                  overdue.safe_observe_mode && wheel.safe_observe_mode() &&
                  overdue.scan_lag_ms > 0,
              "WatchdogFailureInjectionTest should surface scan lag as INF_E_WATCHDOG_SCAN_OVERDUE and switch the deadline wheel into safe_observe_mode");
}

void test_watchdog_failure_injection_rejects_heartbeat_storm_after_capacity_is_exhausted() {
  using dasall::contracts::ResultCode;
  using dasall::infra::watchdog::HeartbeatIngestor;
  using dasall::infra::watchdog::HeartbeatRegistry;
  using dasall::tests::support::assert_true;

  HeartbeatRegistry registry(8U);
  assert_true(registry.register_entity(make_descriptor("runtime.worker.a", "runtime")).ok,
              "WatchdogFailureInjectionTest requires runtime.worker.a to be registered before storm injection");
  assert_true(registry.register_entity(make_descriptor("runtime.worker.b", "runtime")).ok,
              "WatchdogFailureInjectionTest requires runtime.worker.b to be registered before storm injection");
  assert_true(registry.register_entity(make_descriptor("runtime.worker.c", "runtime")).ok,
              "WatchdogFailureInjectionTest requires runtime.worker.c to be registered before storm injection");

  HeartbeatIngestor ingestor(&registry, 1U);
  assert_true(ingestor.ingest(make_sample("runtime.worker.a", 1, 1000, 4000)).ok,
              "WatchdogFailureInjectionTest requires the first entity to occupy the only tracked heartbeat slot before the storm is injected");

  const auto second = ingestor.ingest(make_sample("runtime.worker.b", 1, 1005, 4005));
  const auto third = ingestor.ingest(make_sample("runtime.worker.c", 1, 1010, 4010));
  const auto status = ingestor.status();

  assert_true(!second.ok && !third.ok && second.result_code.has_value() &&
                  third.result_code.has_value() &&
                  *second.result_code == ResultCode::ValidationFieldMissing &&
                  *third.result_code == ResultCode::ValidationFieldMissing,
              "WatchdogFailureInjectionTest should reject overflow heartbeats once the tracked entity budget is exhausted by the injected storm");
  assert_true(second.error.has_value() &&
                  second.error->details.message.find("max_tracked_entities") != std::string::npos &&
                  status.rejected_total == 2 && status.accepted_total == 1 &&
                  ingestor.tracked_entity_count() == 1,
              "WatchdogFailureInjectionTest should keep heartbeat storm pressure observable through the ingestor status counters and leave the accepted sample set unchanged");
}

}  // namespace

int main() {
  try {
    test_watchdog_failure_injection_buffers_publish_failures_locally();
    test_watchdog_failure_injection_enters_safe_observe_mode_after_scan_lag();
    test_watchdog_failure_injection_rejects_heartbeat_storm_after_capacity_is_exhausted();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}