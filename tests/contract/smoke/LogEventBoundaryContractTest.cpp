#include <exception>
#include <iostream>
#include <string>

#include "LogEvent.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

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

}  // namespace

int main() {
  try {
    test_log_event_keeps_contract_identifiers_as_plain_attrs();
    test_log_event_redaction_boundary_does_not_require_contract_expansion();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}