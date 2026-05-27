#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "logging/LoggingFacade.h"
#include "support/TestAssertions.h"

namespace {

class CapturingRecoverySink final : public dasall::infra::logging::ILogRecoverySink {
 public:
  dasall::infra::logging::LogWriteResult write(
      const dasall::infra::logging::LogEvent& event) override {
    written_events_.push_back(event);
    return dasall::infra::logging::LogWriteResult::success();
  }

  [[nodiscard]] const std::vector<dasall::infra::logging::LogEvent>& written_events() const {
    return written_events_;
  }

 private:
  std::vector<dasall::infra::logging::LogEvent> written_events_;
};

class FailingDispatchBackend final : public dasall::infra::logging::ILogDispatchBackend {
 public:
  dasall::infra::logging::LogWriteResult dispatch(
      const dasall::infra::logging::LogEvent&) override {
    ++dispatch_attempts_;
    return dasall::infra::logging::LogWriteResult::failure(
        dasall::contracts::ResultCode::ProviderTimeout,
        "primary backend unavailable",
        "logging.dispatch",
        "FailingDispatchBackend");
  }

  dasall::infra::logging::LogWriteResult flush(
      const dasall::infra::logging::LogFlushDeadline&) override {
    return dasall::infra::logging::LogWriteResult::success();
  }

  [[nodiscard]] std::size_t dispatch_attempts() const {
    return dispatch_attempts_;
  }

 private:
  std::size_t dispatch_attempts_ = 0;
};

[[nodiscard]] dasall::infra::logging::LogEvent make_event(std::string message) {
  return dasall::infra::logging::LogEvent{
      .level = dasall::infra::logging::LogLevel::Error,
      .module = std::string("runtime"),
      .message = std::move(message),
      .attrs = {{"request_id", "req-log-fallback-001"}},
      .ts = 1712300400001,
  };
}

void test_logging_facade_routes_direct_sink_failures_to_fallback_and_stays_degraded() {
  using dasall::infra::logging::LoggingErrorCode;
  using dasall::infra::logging::LoggingFacade;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto fallback_sink = std::make_shared<CapturingRecoverySink>();
  auto failing_backend = std::make_unique<FailingDispatchBackend>();
  auto* backend_ptr = failing_backend.get();
  LoggingFacade facade(std::move(failing_backend), fallback_sink);

  assert_true(facade.init().ok,
              "LoggingSinkFallbackTest should initialize the logging facade before exercising fallback routing");

  const auto first_result = facade.log(make_event("fallback degraded write"));
  assert_true(first_result.ok,
              "LoggingSinkFallbackTest should degrade to fallback success when direct dispatch fails");
  assert_true(facade.is_degraded() && facade.fallback_active(),
              "LoggingSinkFallbackTest should mark the facade degraded once fallback routing activates");
  assert_true(facade.last_recovery_error_code() == LoggingErrorCode::SinkIo,
              "LoggingSinkFallbackTest should retain LOG_E_SINK_IO after direct dispatch fallback");
  assert_equal(1,
               static_cast<int>(fallback_sink->written_events().size()),
               "LoggingSinkFallbackTest should persist the failed direct-dispatch record through the fallback sink");
  assert_equal(1,
               static_cast<int>(backend_ptr->dispatch_attempts()),
               "LoggingSinkFallbackTest should attempt the primary backend exactly once before degrading");

  const auto second_result = facade.log(make_event("fallback steady-state write"));
  assert_true(second_result.ok,
              "LoggingSinkFallbackTest should keep accepting writes through the fallback path while degraded");
  assert_equal(2,
               static_cast<int>(fallback_sink->written_events().size()),
               "LoggingSinkFallbackTest should keep routing subsequent degraded writes to the fallback sink");
  assert_equal(1,
               static_cast<int>(backend_ptr->dispatch_attempts()),
               "LoggingSinkFallbackTest should stop re-touching the failed primary backend while degraded");
  assert_true(fallback_sink->written_events().front().message.find("dasall.logging.event.v1") !=
                  std::string::npos,
              "LoggingSinkFallbackTest should write the structured record into the fallback sink rather than the raw payload");
}

void test_logging_facade_routes_format_failures_to_minimal_fallback_records() {
  using dasall::infra::logging::LoggingErrorCode;
  using dasall::infra::logging::LoggingFacade;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto fallback_sink = std::make_shared<CapturingRecoverySink>();
  LoggingFacade facade(nullptr, fallback_sink);
  facade.set_force_format_failure_for_tests(true);

  assert_true(facade.init().ok,
              "LoggingSinkFallbackTest should initialize the logging facade before forcing a formatter failure");

  const auto log_result = facade.log(make_event("format failure fallback"));
  assert_true(log_result.ok,
              "LoggingSinkFallbackTest should degrade to fallback success when formatter output cannot be built");
  assert_true(facade.is_degraded() && facade.fallback_active(),
              "LoggingSinkFallbackTest should mark formatter failures as degraded fallback events");
  assert_true(facade.last_recovery_error_code() == LoggingErrorCode::FormatInvalid,
              "LoggingSinkFallbackTest should retain LOG_E_FORMAT_INVALID when formatter recovery activates");
  assert_equal(1,
               static_cast<int>(fallback_sink->written_events().size()),
               "LoggingSinkFallbackTest should persist exactly one minimal fallback record for a formatter failure");
  assert_true(fallback_sink->written_events().front().message ==
                  "format failure fallback",
              "LoggingSinkFallbackTest should preserve the pre-format payload in the minimal fallback record");
  assert_true(fallback_sink->written_events().front().attrs.empty(),
              "LoggingSinkFallbackTest should strip formatter-generated attrs from the minimal fallback record");
}

}  // namespace

int main() {
  try {
    test_logging_facade_routes_direct_sink_failures_to_fallback_and_stays_degraded();
    test_logging_facade_routes_format_failures_to_minimal_fallback_records();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}