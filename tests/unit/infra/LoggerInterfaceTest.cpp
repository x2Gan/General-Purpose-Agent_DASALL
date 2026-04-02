#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "logging/ILogger.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

class NullLogger final : public dasall::infra::logging::ILogger {
 public:
  dasall::infra::logging::LogWriteResult log(
      const dasall::infra::logging::LogEvent& event) override {
    if (!event.attrs_are_serializable()) {
      return dasall::infra::logging::LogWriteResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "log event attrs must stay serializable",
          "logging.log",
          "NullLogger");
    }

    return dasall::infra::logging::LogWriteResult::success();
  }

  dasall::infra::logging::LogWriteResult flush(
      const dasall::infra::logging::LogFlushDeadline& deadline) override {
    if (!deadline.is_valid()) {
      return dasall::infra::logging::LogWriteResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "flush deadline must be greater than zero",
          "logging.flush",
          "NullLogger");
    }

    return dasall::infra::logging::LogWriteResult::success();
  }

  void set_level(dasall::infra::logging::LogLevel level) override {
    current_level_ = level;
  }

  [[nodiscard]] dasall::infra::logging::LogLevel current_level() const {
    return current_level_;
  }

 private:
  dasall::infra::logging::LogLevel current_level_ =
      dasall::infra::logging::LogLevel::Info;
};

void test_logging_component_logger_exposes_single_canonical_interface() {
  using dasall::infra::logging::ILogger;
  using dasall::infra::logging::LogEvent;
  using dasall::infra::logging::LogLevel;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(&ILogger::set_level), void (ILogger::*)(LogLevel)>);

  NullLogger logger;
  ILogger& interface = logger;
  interface.set_level(LogLevel::Error);

  assert_true(logger.current_level() == LogLevel::Error,
              "logging::ILogger should expose dynamic level adjustment from its canonical interface");

  const LogEvent event{
      .level = LogLevel::Error,
      .module = std::string("logging"),
      .message = std::string("component logger ready"),
      .attrs = {{"request_id", "req-logger-002"}},
      .ts = 200,
  };

  const auto log_result = interface.log(event);
  assert_true(log_result.ok,
              "logging::ILogger should retain the frozen log contract on its canonical interface");
}

void test_logger_interface_accepts_log_event_and_placeholder_deadline() {
  using dasall::infra::logging::LogEvent;
  using dasall::infra::logging::LogFlushDeadline;
  using dasall::infra::logging::LogLevel;
  using dasall::tests::support::assert_true;

  NullLogger logger;

  const LogEvent event{
      .level = LogLevel::Info,
      .module = std::string("runtime"),
      .message = std::string("logger interface ready"),
      .attrs = {{"request_id", "req-logger-001"}},
      .ts = 100,
  };

  const auto log_result = logger.log(event);
  assert_true(log_result.ok,
              "ILogger skeleton should accept LogEvent after LogEvent freeze");

  const auto flush_result = logger.flush(LogFlushDeadline{.timeout_ms = 500});
  assert_true(flush_result.ok,
              "ILogger skeleton should accept a positive placeholder flush deadline");
}

void test_logger_interface_reports_validation_failures_observably() {
  using dasall::infra::logging::LogEvent;
  using dasall::infra::logging::LogFlushDeadline;
  using dasall::infra::logging::LogLevel;
  using dasall::tests::support::assert_true;

  NullLogger logger;

  const LogEvent invalid_event{
      .level = LogLevel::Warn,
      .module = std::string("logging"),
      .message = std::string("invalid attrs"),
      .attrs = {{"", "bad"}},
      .ts = 100,
  };

  const auto log_result = logger.log(invalid_event);
  assert_true(!log_result.ok,
              "ILogger skeleton should reject non-serializable log attrs");
  assert_true(log_result.references_only_contract_error_types(),
              "log validation failures should stay within contracts error types");

  const auto flush_result = logger.flush(LogFlushDeadline{});
  assert_true(!flush_result.ok,
              "ILogger skeleton should reject an unset flush deadline placeholder");
  assert_true(flush_result.references_only_contract_error_types(),
              "flush validation failures should stay within contracts error types");
}

}  // namespace

int main() {
  try {
    test_logging_component_logger_exposes_single_canonical_interface();
    test_logger_interface_accepts_log_event_and_placeholder_deadline();
    test_logger_interface_reports_validation_failures_observably();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}