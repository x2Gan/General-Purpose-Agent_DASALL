#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "watchdog/TimeoutDecision.h"
#include "watchdog/TimeoutEventPublisher.h"
#include "watchdog/WatchdogErrors.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

class RecordingTimeoutEventSink final
    : public dasall::infra::watchdog::ITimeoutEventSink {
 public:
  dasall::infra::watchdog::TimeoutEventDispatchResult publish_timeout_event(
      const dasall::infra::watchdog::TimeoutEvent& event) override {
    events.push_back(event);
    if (!scripted_results.empty()) {
      const auto result = scripted_results.front();
      scripted_results.pop_front();
      return result;
    }

    return dasall::infra::watchdog::TimeoutEventDispatchResult::success(
        "timeout-event://dispatch/1");
  }

  std::deque<dasall::infra::watchdog::TimeoutEventDispatchResult> scripted_results;
  std::vector<dasall::infra::watchdog::TimeoutEvent> events;
};

[[nodiscard]] dasall::infra::watchdog::TimeoutDecision make_decision(
    dasall::infra::watchdog::WatchdogTimeoutLevel timeout_level,
    std::uint32_t consecutive_miss,
    std::string evidence_ref = "watchdog://timeout/runtime.main_loop") {
  return dasall::infra::watchdog::TimeoutDecision{
      .entity_id = std::string("runtime.main_loop"),
      .timeout_level = timeout_level,
      .consecutive_miss = consecutive_miss,
      .reason_code = dasall::contracts::ResultCode::ProviderTimeout,
      .evidence_ref = std::move(evidence_ref),
  };
}

void test_timeout_event_publisher_emits_critical_timeout_with_unknown_trace_fields() {
  using dasall::infra::watchdog::TimeoutEventPublisher;
  using dasall::infra::watchdog::WatchdogServiceConfig;
  using dasall::infra::watchdog::WatchdogTimeoutLevel;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto sink = std::make_shared<RecordingTimeoutEventSink>();
  WatchdogServiceConfig config;
  config.event_queue_size = 4;

  TimeoutEventPublisher publisher(sink, config);
  const auto result = publisher.publish_timeout(
      make_decision(WatchdogTimeoutLevel::Critical, 3));
  const auto status = publisher.status();

  assert_true(result.emitted && result.is_valid(),
              "TimeoutEventPublisher should emit critical timeout decisions through the frozen timeout event sink abstraction");
  assert_true(status.is_valid() && status.published_total == 1 &&
                  status.publish_fail_total == 0 && !status.degraded,
              "TimeoutEventPublisher should remain healthy after a successful critical publish");
  assert_equal(1,
               static_cast<int>(sink->events.size()),
               "TimeoutEventPublisher should dispatch exactly one timeout event for a critical decision");
  assert_equal(std::string("watchdog-timeout://runtime.main_loop/critical/3"),
               sink->events.front().event_id,
               "TimeoutEventPublisher should freeze timeout event ids to entity/level/miss format");
  assert_true(sink->events.front().trace_id == "unknown" &&
                  sink->events.front().session_id == "unknown" &&
                  sink->events.front().task_id == "unknown",
              "TimeoutEventPublisher should keep missing trace/session/task identifiers inside the frozen unknown sentinel boundary");
}

void test_timeout_event_publisher_skips_warning_only_timeouts() {
  using dasall::infra::watchdog::TimeoutEventPublisher;
  using dasall::infra::watchdog::WatchdogTimeoutLevel;
  using dasall::tests::support::assert_true;

  auto sink = std::make_shared<RecordingTimeoutEventSink>();
  TimeoutEventPublisher publisher(sink);

  const auto result = publisher.publish_timeout(
      make_decision(WatchdogTimeoutLevel::Warning, 1));
  const auto status = publisher.status();

  assert_true(!result.emitted && result.skipped && result.is_valid(),
              "TimeoutEventPublisher should leave warning-only timeout decisions to metrics and skip event bus emission");
  assert_true(sink->events.empty() && status.skipped_total == 1 &&
                  status.publish_fail_total == 0,
              "TimeoutEventPublisher should count warning decisions as skips without recording publish failures");
}

void test_timeout_event_publisher_buffers_publish_failures_and_counts_them() {
  using dasall::infra::watchdog::TimeoutEventDispatchResult;
  using dasall::infra::watchdog::TimeoutEventPublisher;
  using dasall::infra::watchdog::watchdog_error_code_name;
  using dasall::infra::watchdog::WatchdogErrorCode;
  using dasall::infra::watchdog::WatchdogServiceConfig;
  using dasall::infra::watchdog::WatchdogTimeoutLevel;
  using dasall::tests::support::assert_true;

  auto sink = std::make_shared<RecordingTimeoutEventSink>();
  sink->scripted_results.push_back(TimeoutEventDispatchResult::failure(
      dasall::contracts::ResultCode::ToolExecutionFailed,
      "synthetic sink failure",
      "watchdog.timeout_event.dispatch",
      "RecordingTimeoutEventSink"));

  WatchdogServiceConfig config;
  config.event_queue_size = 2;

  TimeoutEventPublisher publisher(sink, config);
  const auto result = publisher.publish_timeout(
      make_decision(WatchdogTimeoutLevel::Fatal, 4));
  const auto status = publisher.status();

  assert_true(!result.emitted && result.buffered && result.is_valid() &&
                  result.references_only_contract_error_types(),
              "TimeoutEventPublisher should keep sink publish failures observable and buffer the timeout event locally");
  assert_true(result.watchdog_code.has_value() &&
                  *result.watchdog_code == WatchdogErrorCode::EventPublishFail &&
                  result.error_info.has_value() &&
                  result.error_info->details.message.find(
                      std::string(watchdog_error_code_name(WatchdogErrorCode::EventPublishFail))) != std::string::npos,
              "TimeoutEventPublisher should map sink failures to the frozen INF_E_WATCHDOG_EVENT_PUBLISH_FAIL token");
  assert_true(status.is_valid() && status.publish_fail_total == 1 &&
                  status.buffered_total == 1 && status.buffered_event_count == 1 &&
                  status.degraded,
              "TimeoutEventPublisher should count publish failures and local fallback buffering in bridge status");
}

}  // namespace

int main() {
  try {
    test_timeout_event_publisher_emits_critical_timeout_with_unknown_trace_fields();
    test_timeout_event_publisher_skips_warning_only_timeouts();
    test_timeout_event_publisher_buffers_publish_failures_and_counts_them();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}