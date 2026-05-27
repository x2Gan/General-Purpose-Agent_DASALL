#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "logging/LoggingFacade.h"
#include "logging/SinkDispatcher.h"
#include "logging/StructuredFormatter.h"
#include "support/TestAssertions.h"

namespace {

void test_logging_facade_redacts_and_formats_before_dispatch() {
  using dasall::infra::InfraContext;
  using dasall::infra::logging::LogEvent;
  using dasall::infra::logging::LogLevel;
  using dasall::infra::logging::LoggingFacade;
  using dasall::infra::logging::SinkDispatcher;
  using dasall::infra::logging::SinkRoute;
  using dasall::infra::logging::StructuredFormatter;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto dispatcher = std::make_unique<SinkDispatcher>();
  auto* dispatcher_ptr = dispatcher.get();
  LoggingFacade facade(std::move(dispatcher));

  assert_true(facade.init(InfraContext{
                  .request_id = std::string("req-int-redaction-001"),
                  .session_id = std::string("session-int-redaction-001"),
                  .trace_id = std::string("trace-int-redaction-001"),
                  .task_id = std::string("task-int-redaction-001"),
                  .parent_task_id = std::string("parent-int-redaction-001"),
                  .lease_id = std::string("lease-int-redaction-001"),
              })
                  .ok,
              "logging facade redaction integration should initialize before dispatch");

  const auto log_result = facade.log(LogEvent{
      .level = LogLevel::Error,
      .module = std::string("runtime"),
      .message = std::string(
          "Authorization: Bearer bearer-secret password=runtime-secret"),
      .attrs = {
          {"event_name", "runtime.secret_filtered"},
          {"api_token", "raw-token-int-001"},
          {"exception_message", "secret=db-secret"},
      },
      .ts = 1712300300001,
  });

  assert_true(log_result.ok,
              "logging facade redaction integration should accept a valid event");
  assert_true(dispatcher_ptr->last_route() == SinkRoute::BasicFile,
              "logging facade redaction integration should preserve runtime routing");

  const auto& dispatched = facade.last_dispatched_event();
  assert_equal("<redacted>", dispatched.attrs.at("api_token"),
               "logging facade should redact sensitive attrs before dispatch");
  assert_equal("dasall.logging.event.v1", dispatched.attrs.at("schema_version"),
               "logging facade should stamp the structured schema version before dispatch");
  assert_equal("trace-int-redaction-001", dispatched.attrs.at("correlation_id"),
               "logging facade should derive correlation_id from enriched context before dispatch");
  assert_true(dispatched.message.find("bearer-secret") == std::string::npos,
              "logging facade should remove bearer secrets from the dispatched payload");
  assert_true(dispatched.message.find("runtime-secret") == std::string::npos,
              "logging facade should remove password payloads from the dispatched payload");
  assert_true(dispatched.message.find("db-secret") == std::string::npos,
              "logging facade should remove exception payloads from the dispatched payload");
  assert_true(dispatched.message.find("<redacted>") != std::string::npos,
              "logging facade should leave explicit redaction markers in the dispatched payload");
  assert_true(dispatched.message.find("\"schema_version\":\"dasall.logging.event.v1\"") !=
                  std::string::npos,
              "logging facade should emit the structured JSON envelope before dispatch");
  assert_equal("trace-int-redaction-001",
               dispatcher_ptr->last_record().event.attrs.at("trace_id"),
               "logging facade should preserve enriched trace_id through the dispatcher");
}

}  // namespace

int main() {
  try {
    test_logging_facade_redacts_and_formats_before_dispatch();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}