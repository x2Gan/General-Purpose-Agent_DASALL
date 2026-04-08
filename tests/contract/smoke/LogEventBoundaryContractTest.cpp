#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "LogEvent.h"
#include "logging/LogTypes.h"
#include "support/TestAssertions.h"

namespace {

template <typename T>
concept HasRequestIdMember = requires {
  &T::request_id;
};

void test_log_event_keeps_contract_identifiers_as_plain_attrs() {
  using dasall::infra::LogEvent;
  using dasall::infra::LogLevel;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const LogEvent event{
      .level = LogLevel::Info,
      .module = std::string("multi_agent"),
      .message = std::string("worker dispatched"),
      .attrs = {
          {"request_id", "req-001"},
          {"session_id", "sess-001"},
          {"trace_id", "trace-001"},
          {"task_id", "task-001"},
      },
      .ts = 777,
  };

  assert_true(event.attrs_are_serializable(),
              "contract identifiers should remain representable as serializable attrs");
  const auto redacted = event.redacted_attrs();
  assert_equal("req-001", redacted.at("request_id"),
               "request_id should remain an infra-owned plain attribute");
  assert_equal("trace-001", redacted.at("trace_id"),
               "trace_id should remain an infra-owned plain attribute");
}

void test_log_event_redaction_boundary_does_not_require_contract_expansion() {
  using dasall::infra::LogEvent;
  using dasall::infra::LogLevel;
  using dasall::tests::support::assert_equal;

  const LogEvent event{
      .level = LogLevel::Warn,
      .module = std::string("logging"),
      .message = std::string("credential filtered"),
      .attrs = {
          {"secret_key", "secret-value"},
          {"password_hint", "pw"},
      },
      .ts = 999,
  };

  const auto redacted = event.redacted_attrs();
  assert_equal("<redacted>", redacted.at("secret_key"),
               "sensitive attrs should be redacted inside infra without contracts changes");
  assert_equal("<redacted>", redacted.at("password_hint"),
               "redaction should remain local to infra attr handling");
}

void test_logging_log_event_alias_stays_inside_infra_boundary_shape() {
  using LoggingLogEvent = dasall::infra::logging::LogEvent;
  using LoggingLogLevel = dasall::infra::logging::LogLevel;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<LoggingLogEvent, dasall::infra::LogEvent>);
  static_assert(std::is_same_v<LoggingLogLevel, dasall::infra::LogLevel>);
  static_assert(!HasRequestIdMember<LoggingLogEvent>);

  const LoggingLogEvent event{
      .level = LoggingLogLevel::Info,
      .module = std::string("multi_agent"),
      .message = std::string("worker lease granted"),
      .attrs = {
          {"request_id", "req-logging-004"},
          {"trace_id", "trace-logging-004"},
      },
      .ts = 8899,
  };

  assert_true(event.category() == "multi_agent",
              "logging::LogEvent should keep category semantics as a view over the frozen module field instead of promoting request_id to a top-level member");
  assert_true(event.attrs_are_serializable() && event.has_timestamp(),
              "logging::LogEvent should keep attrs and timestamp as the only stable structured-record extensibility points at this freeze stage");
}

}  // namespace

int main() {
  try {
    test_log_event_keeps_contract_identifiers_as_plain_attrs();
    test_log_event_redaction_boundary_does_not_require_contract_expansion();
    test_logging_log_event_alias_stays_inside_infra_boundary_shape();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}