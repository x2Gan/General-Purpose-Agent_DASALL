#include <exception>
#include <iostream>
#include <string>

#include "logging/RedactionFilter.h"
#include "support/TestAssertions.h"

namespace {

void test_logging_redaction_filter_redacts_sensitive_message_and_attr_values() {
  using dasall::infra::logging::LogEvent;
  using dasall::infra::logging::LogLevel;
  using dasall::infra::logging::RedactionFilter;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  RedactionFilter filter;
  const auto filtered = filter.apply(LogEvent{
      .level = LogLevel::Error,
      .module = std::string("runtime"),
      .message = std::string(
          "Authorization: Bearer bearer-secret password=top-secret"),
      .attrs = {
          {"request_id", "req-redact-001"},
          {"api_token", "raw-token-001"},
          {"exception_message", "db auth failed secret=pg-secret"},
      },
      .ts = 1712300100001,
  });

  assert_equal("<redacted>", filtered.attrs.at("api_token"),
               "redaction filter should fully redact sensitive attr keys");
  assert_equal("req-redact-001", filtered.attrs.at("request_id"),
               "redaction filter should preserve non-sensitive correlation attrs");
  assert_true(filtered.message.find("bearer-secret") == std::string::npos,
              "redaction filter should remove bearer tokens from message text");
  assert_true(filtered.message.find("top-secret") == std::string::npos,
              "redaction filter should remove password payloads from message text");
  assert_true(filtered.message.find("<redacted>") != std::string::npos,
              "redaction filter should leave an explicit redaction marker in message text");
  assert_true(filtered.attrs.at("exception_message").find("pg-secret") ==
                  std::string::npos,
              "redaction filter should remove secret payloads from exception attrs");
  assert_true(filtered.attrs.at("exception_message").find("<redacted>") !=
                  std::string::npos,
              "redaction filter should redact exception attrs with the shared marker");
}

void test_logging_redaction_filter_preserves_safe_fields() {
  using dasall::infra::logging::LogEvent;
  using dasall::infra::logging::LogLevel;
  using dasall::infra::logging::RedactionFilter;
  using dasall::tests::support::assert_equal;

  RedactionFilter filter;
  const auto filtered = filter.apply(LogEvent{
      .level = LogLevel::Info,
      .module = std::string("memory"),
      .message = std::string("maintenance completed"),
      .attrs = {
          {"event_name", "maintenance.completed"},
          {"trace_id", "trace-redact-001"},
      },
      .ts = 1712300100002,
  });

  assert_equal("maintenance completed", filtered.message,
               "redaction filter should preserve safe message text");
  assert_equal("maintenance.completed", filtered.attrs.at("event_name"),
               "redaction filter should preserve safe event attrs");
  assert_equal("trace-redact-001", filtered.attrs.at("trace_id"),
               "redaction filter should preserve safe trace attrs");
}

}  // namespace

int main() {
  try {
    test_logging_redaction_filter_redacts_sensitive_message_and_attr_values();
    test_logging_redaction_filter_preserves_safe_fields();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}