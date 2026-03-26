#include <exception>
#include <iostream>
#include <string>

#include "LogEvent.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_log_event_accepts_serializable_attrs_and_optional_message() {
  using dasall::infra::LogEvent;
  using dasall::infra::LogLevel;
  using dasall::tests::support::assert_true;

  const LogEvent event{
      .level = LogLevel::Info,
      .module = std::string("runtime"),
      .message = std::string(),
      .attrs = {
          {"request_id", "req-001"},
          {"trace_id", "trace-001"},
      },
      .ts = 123456,
  };

  assert_true(event.attrs_are_serializable(),
              "string attribute map should be serializable");
  assert_true(event.has_timestamp(), "non-negative timestamp should be treated as valid");
  assert_true(event.category() == event.module,
              "module should remain the stable category alias during L2 freeze");
}

void test_log_event_rejects_empty_attr_keys_for_serialization() {
  using dasall::infra::LogEvent;
  using dasall::infra::LogLevel;
  using dasall::tests::support::assert_true;

  const LogEvent event{
      .level = LogLevel::Warn,
      .module = std::string("logging"),
      .message = std::string("queue pressure"),
      .attrs = {
          {"", "invalid"},
          {"queue_depth", "42"},
      },
      .ts = 123456,
  };

  assert_true(!event.attrs_are_serializable(),
              "empty attribute keys should fail the serializable attrs guard");
}

void test_log_event_redacts_sensitive_attrs_without_touching_context_ids() {
  using dasall::infra::LogEvent;
  using dasall::infra::LogLevel;
  using dasall::tests::support::assert_equal;

  const LogEvent event{
      .level = LogLevel::Error,
      .module = std::string("secret"),
      .message = std::string("rotation failed"),
      .attrs = {
          {"request_id", "req-001"},
          {"api_token", "raw-token"},
          {"authorization", "Bearer abc"},
      },
      .ts = 123456,
  };

  const auto redacted = event.redacted_attrs();

  assert_equal("req-001", redacted.at("request_id"),
               "non-sensitive context ids should remain unchanged");
  assert_equal("<redacted>", redacted.at("api_token"),
               "sensitive token attrs should be redacted");
  assert_equal("<redacted>", redacted.at("authorization"),
               "authorization attrs should be redacted");
}

}  // namespace

int main() {
  try {
    test_log_event_accepts_serializable_attrs_and_optional_message();
    test_log_event_rejects_empty_attr_keys_for_serialization();
    test_log_event_redacts_sensitive_attrs_without_touching_context_ids();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}