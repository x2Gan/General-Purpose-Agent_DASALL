#include <exception>
#include <iostream>
#include <string>

#include "logging/StructuredFormatter.h"
#include "support/TestAssertions.h"

namespace {

void test_logging_structured_formatter_emits_schema_correlation_and_json_message() {
  using dasall::infra::logging::LogEvent;
  using dasall::infra::logging::LogLevel;
  using dasall::infra::logging::StructuredFormatter;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  StructuredFormatter formatter;
  const auto formatted = formatter.format(LogEvent{
      .level = LogLevel::Warn,
      .module = std::string("runtime"),
      .message = std::string("queue pressure"),
      .attrs = {
          {"request_id", "req-format-001"},
          {"trace_id", "trace-format-001"},
          {"task_id", "task-format-001"},
          {"event_kind", "formatter_smoke"},
      },
      .ts = 1712300200001,
  });

  assert_equal("dasall.logging.event.v1", formatted.attrs.at("schema_version"),
               "structured formatter should stamp the frozen schema version");
  assert_equal("trace-format-001", formatted.attrs.at("correlation_id"),
               "structured formatter should prefer trace_id as correlation_id");
  assert_equal("trace-format-001|task-format-001|runtime|1712300200001",
               formatted.attrs.at("idempotency_key"),
               "structured formatter should derive a stable idempotency key from the frozen tuple");
  assert_true(formatted.message.find("\"schema_version\":\"dasall.logging.event.v1\"") !=
                  std::string::npos,
              "structured formatter should encode the schema version into the JSON message");
  assert_true(formatted.message.find("\"module\":\"runtime\"") !=
                  std::string::npos,
              "structured formatter should encode module into the JSON message");
  assert_true(formatted.message.find("\"message\":\"queue pressure\"") !=
                  std::string::npos,
              "structured formatter should preserve the rendered message inside the JSON envelope");
  assert_true(formatted.message.find("\"event_kind\":\"formatter_smoke\"") !=
                  std::string::npos,
              "structured formatter should serialize attrs into the JSON envelope");
}

void test_logging_structured_formatter_falls_back_to_unknown_correlation() {
  using dasall::infra::logging::LogContext;
  using dasall::infra::logging::LogEvent;
  using dasall::infra::logging::LogLevel;
  using dasall::infra::logging::StructuredFormatter;
  using dasall::tests::support::assert_equal;

  StructuredFormatter formatter;
  const auto formatted = formatter.format(LogEvent{
      .level = LogLevel::Info,
      .module = std::string("memory"),
      .message = std::string("maintenance completed"),
      .attrs = {{"event_name", "maintenance.completed"}},
      .ts = std::nullopt,
  });

  assert_equal(std::string(LogContext::kUnknownIdentifier),
               formatted.attrs.at("correlation_id"),
               "structured formatter should fall back to unknown correlation when canonical ids are absent");
}

}  // namespace

int main() {
  try {
    test_logging_structured_formatter_emits_schema_correlation_and_json_message();
    test_logging_structured_formatter_falls_back_to_unknown_correlation();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}