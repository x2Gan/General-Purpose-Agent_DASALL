#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "logging/LoggingRecovery.h"
#include "support/TestAssertions.h"

namespace {

class ScriptedRecoverySink final : public dasall::infra::logging::ILogRecoverySink {
 public:
  void push_result(dasall::infra::logging::LogWriteResult result) {
    scripted_results_.push_back(std::move(result));
  }

  dasall::infra::logging::LogWriteResult write(
      const dasall::infra::logging::LogEvent& event) override {
    written_events_.push_back(event);
    if (scripted_results_.empty()) {
      return dasall::infra::logging::LogWriteResult::success();
    }

    auto result = scripted_results_.front();
    scripted_results_.pop_front();
    return result;
  }

  [[nodiscard]] const std::vector<dasall::infra::logging::LogEvent>& written_events() const {
    return written_events_;
  }

 private:
  std::deque<dasall::infra::logging::LogWriteResult> scripted_results_;
  std::vector<dasall::infra::logging::LogEvent> written_events_;
};

dasall::infra::logging::LogEvent make_event(std::string message) {
  return dasall::infra::logging::LogEvent{
      .level = dasall::infra::logging::LogLevel::Error,
      .module = std::string("runtime"),
      .message = std::move(message),
      .attrs = {
          {"request_id", "req-log-011"},
          {"trace_id", "trace-log-011"},
      },
      .ts = 1711972201000,
  };
}

dasall::infra::logging::LogWriteResult make_sink_failure(
    dasall::infra::logging::LoggingErrorCode code,
    std::string message) {
  const auto mapping = dasall::infra::logging::map_logging_error_code(code);
  return dasall::infra::logging::LogWriteResult::failure(
      mapping.result_code,
      std::move(message),
      "logging.recovery.test",
      "ScriptedRecoverySink");
}

void test_logging_recovery_marks_degraded_and_uses_fallback_on_sink_failure() {
  using dasall::infra::logging::LoggingErrorCode;
  using dasall::infra::logging::LoggingRecovery;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto primary_sink = std::make_shared<ScriptedRecoverySink>();
  auto fallback_sink = std::make_shared<ScriptedRecoverySink>();
  primary_sink->push_result(make_sink_failure(LoggingErrorCode::SinkIo,
                                              "primary sink unavailable"));

  LoggingRecovery recovery(primary_sink, fallback_sink);
  const auto result = recovery.write(make_event("sink failure degraded"));

  assert_true(result.has_consistent_state() && result.is_degraded_success(),
              "primary sink failures should trigger a degraded fallback success path");
  assert_true(recovery.is_degraded(),
              "sink IO failures should mark logging recovery as degraded");
  assert_true(recovery.fallback_active(),
              "sink IO failures should activate the fallback sink path");
  assert_true(recovery.last_error_code() == LoggingErrorCode::SinkIo,
              "sink IO failures should retain LOG_E_SINK_IO as the last observed logging error");
  assert_equal(1,
               static_cast<int>(fallback_sink->written_events().size()),
               "fallback sink should receive the degraded write after primary failure");
}

void test_logging_recovery_clears_degraded_state_after_successful_retry() {
  using dasall::infra::logging::LoggingErrorCode;
  using dasall::infra::logging::LoggingRecovery;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto primary_sink = std::make_shared<ScriptedRecoverySink>();
  auto fallback_sink = std::make_shared<ScriptedRecoverySink>();
  primary_sink->push_result(make_sink_failure(LoggingErrorCode::SinkIo,
                                              "primary sink unavailable"));
  primary_sink->push_result(dasall::infra::logging::LogWriteResult::success());

  LoggingRecovery recovery(primary_sink, fallback_sink);
  assert_true(recovery.write(make_event("initial degraded write")).is_degraded_success(),
              "initial primary failure should enter degraded mode before retry testing");

  const auto retry = recovery.retry_primary_sink(make_event("recovery probe"));
  assert_true(retry.has_consistent_state() && retry.is_success(),
              "successful retry should clear degraded mode and return a recovered success");
  assert_true(retry.recovery_attempted && retry.recovery_succeeded,
              "successful retry should be observable as a recovery attempt that succeeded");
  assert_true(!recovery.is_degraded() && !recovery.fallback_active(),
              "successful retry should clear degraded state and deactivate fallback routing");
  assert_equal(1,
               static_cast<int>(recovery.retry_attempt_total()),
               "retry entry should record one retry attempt");
  assert_equal(1,
               static_cast<int>(recovery.recovery_success_total()),
               "successful retry should increment the recovery-success counter");
}

void test_logging_recovery_keeps_degraded_state_when_retry_continues_to_fail() {
  using dasall::infra::logging::LoggingErrorCode;
  using dasall::infra::logging::LoggingRecovery;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto primary_sink = std::make_shared<ScriptedRecoverySink>();
  auto fallback_sink = std::make_shared<ScriptedRecoverySink>();
  primary_sink->push_result(make_sink_failure(LoggingErrorCode::SinkIo,
                                              "primary sink unavailable"));
  primary_sink->push_result(make_sink_failure(LoggingErrorCode::SinkIo,
                                              "primary sink still unavailable"));

  LoggingRecovery recovery(primary_sink, fallback_sink);
  assert_true(recovery.write(make_event("initial degraded write")).is_degraded_success(),
              "initial primary failure should enter degraded mode before retry failure testing");

  const auto retry = recovery.retry_primary_sink(make_event("retry still failing"));
  assert_true(retry.has_consistent_state() && retry.is_degraded_success(),
              "failed retry should keep the system in degraded success mode through fallback writes");
  assert_true(retry.recovery_attempted && !retry.recovery_succeeded,
              "failed retry should remain observable as an attempted but unsuccessful recovery");
  assert_true(recovery.is_degraded() && recovery.fallback_active(),
              "failed retry should preserve degraded fallback routing");
  assert_equal(1,
               static_cast<int>(recovery.retry_attempt_total()),
               "failed retry should still increase the retry-attempt counter");
  assert_equal(1,
               static_cast<int>(recovery.recovery_failure_total()),
               "failed retry should increase the recovery-failure counter");
  assert_equal(2,
               static_cast<int>(fallback_sink->written_events().size()),
               "fallback sink should receive both the initial degraded write and the retry-failure degraded write");
}

void test_logging_recovery_uses_minimal_fallback_record_for_format_failures() {
  using dasall::infra::logging::LoggingErrorCode;
  using dasall::infra::logging::LoggingRecovery;
  using dasall::tests::support::assert_true;

  auto primary_sink = std::make_shared<ScriptedRecoverySink>();
  auto fallback_sink = std::make_shared<ScriptedRecoverySink>();
  LoggingRecovery recovery(primary_sink, fallback_sink);

  const auto result = recovery.handle_format_failure(make_event("format degraded"));
  assert_true(result.has_consistent_state() && result.is_degraded_success(),
              "format failures should use a degraded fallback success path instead of crashing the pipeline");
  assert_true(recovery.last_error_code() == LoggingErrorCode::FormatInvalid,
              "format failures should retain LOG_E_FORMAT_INVALID as the last observed logging error");
  assert_true(recovery.has_last_fallback_event(),
              "format failures should retain the downgraded fallback record for observability");
  assert_true(recovery.last_fallback_event().attrs.empty(),
              "format failure fallback records should drop attrs and keep a minimal field set only");
}

}  // namespace

int main() {
  try {
    test_logging_recovery_marks_degraded_and_uses_fallback_on_sink_failure();
    test_logging_recovery_clears_degraded_state_after_successful_retry();
    test_logging_recovery_keeps_degraded_state_when_retry_continues_to_fail();
    test_logging_recovery_uses_minimal_fallback_record_for_format_failures();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}