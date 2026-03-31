#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "logging/ILogger.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

class NullLogger final : public dasall::infra::logging::ILogger {
 public:
  dasall::infra::LogWriteResult log(const dasall::infra::LogEvent& event) override {
    if (!event.attrs_are_serializable()) {
      return dasall::infra::LogWriteResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "log event attrs must stay serializable",
          "logging.log",
          "NullLogger");
    }

    return dasall::infra::LogWriteResult::success();
  }

  dasall::infra::LogWriteResult flush(
      const dasall::infra::LogFlushDeadline& deadline) override {
    if (!deadline.is_valid()) {
      return dasall::infra::LogWriteResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "flush deadline must be greater than zero",
          "logging.flush",
          "NullLogger");
    }

    return dasall::infra::LogWriteResult::success();
  }

  void set_level(dasall::infra::logging::LogLevel level) override {
    current_level_ = level;
  }

  [[nodiscard]] dasall::infra::LogLevel current_level() const {
    return current_level_;
  }

 private:
  dasall::infra::LogLevel current_level_ = dasall::infra::LogLevel::Info;
};

void test_logging_component_logger_extends_base_interface_with_level_control() {
  using dasall::infra::ILogger;
  using dasall::infra::LogEvent;
  using dasall::infra::LogLevel;
  using dasall::tests::support::assert_true;

  static_assert(std::is_base_of_v<ILogger, dasall::infra::logging::ILogger>);

  NullLogger logger;
  logger.set_level(dasall::infra::logging::LogLevel::Error);

  assert_true(logger.current_level() == LogLevel::Error,
              "logging::ILogger should freeze dynamic level adjustment without changing the base logger contract");

  ILogger& base_logger = logger;
  const LogEvent event{
      .level = LogLevel::Error,
      .module = std::string("logging"),
      .message = std::string("component logger ready"),
      .attrs = {{"request_id", "req-logger-002"}},
      .ts = 200,
  };

  const auto log_result = base_logger.log(event);
  assert_true(log_result.ok,
              "logging::ILogger should remain substitutable for the frozen base infra::ILogger log contract");
}

void test_logger_interface_accepts_log_event_and_placeholder_deadline() {
  using dasall::infra::LogEvent;
  using dasall::infra::LogFlushDeadline;
  using dasall::infra::LogLevel;
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
  using dasall::infra::LogEvent;
  using dasall::infra::LogFlushDeadline;
  using dasall::infra::LogLevel;
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
    test_logging_component_logger_extends_base_interface_with_level_control();
    test_logger_interface_accepts_log_event_and_placeholder_deadline();
    test_logger_interface_reports_validation_failures_observably();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}