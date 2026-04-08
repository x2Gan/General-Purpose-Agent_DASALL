#include <exception>
#include <iostream>
#include <string>

#include "logging/SinkDispatcher.h"
#include "support/TestAssertions.h"

namespace {

dasall::infra::logging::LogEvent make_event(std::string module) {
  return dasall::infra::logging::LogEvent{
      .level = dasall::infra::logging::LogLevel::Info,
      .module = std::move(module),
      .message = std::string("sink dispatcher skeleton"),
      .attrs = {{"request_id", "req-log-007"}},
      .ts = 1711968607000,
  };
}

void test_sink_dispatcher_routes_runtime_events_to_basic_file_path() {
  using dasall::infra::logging::SinkDispatcher;
  using dasall::infra::logging::SinkRoute;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  SinkDispatcher dispatcher;

  const auto result = dispatcher.dispatch(make_event("runtime"));
  assert_true(result.ok,
              "sink dispatcher should accept a basic runtime event");
  assert_true(dispatcher.has_last_record(),
              "sink dispatcher should retain the routed record in the skeleton path");
  assert_true(dispatcher.last_route() == SinkRoute::BasicFile,
              "runtime category should stay on the basic file route");
  assert_equal(1,
               static_cast<int>(dispatcher.dispatched_record_count(SinkRoute::BasicFile)),
               "basic route counter should advance for runtime events");
  assert_equal(1,
               static_cast<int>(dispatcher.queue_depth()),
               "runtime route should enqueue exactly one record into the async queue skeleton");
}

void test_sink_dispatcher_routes_audit_events_without_mutating_attrs() {
  using dasall::infra::logging::SinkDispatcher;
  using dasall::infra::logging::SinkRoute;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  SinkDispatcher dispatcher;

  auto event = make_event("audit");
  event.attrs.insert_or_assign("evidence_ref", "audit-ev-007");

  const auto result = dispatcher.dispatch(event);
  assert_true(result.ok,
              "sink dispatcher should route audit-tagged events successfully");
  assert_true(dispatcher.last_route() == SinkRoute::Audit,
              "audit-tagged events should use the audit route");
  assert_equal(1,
               static_cast<int>(dispatcher.dispatched_record_count(SinkRoute::Audit)),
               "audit route counter should advance for audit-tagged events");
  assert_equal(0,
               static_cast<int>(dispatcher.dropped_total()),
               "audit routing should not report dropped records while capacity is available");
  assert_true(dispatcher.last_record().event.attrs.at("evidence_ref") == "audit-ev-007",
              "sink dispatcher should preserve audit evidence attrs while routing");
}

void test_sink_dispatcher_validates_input_and_flush_deadlines() {
  using dasall::infra::logging::LogFlushDeadline;
  using dasall::infra::logging::SinkDispatcher;
  using dasall::tests::support::assert_true;

  SinkDispatcher dispatcher;

  const auto invalid_dispatch = dispatcher.dispatch(dasall::infra::logging::LogEvent{
      .level = dasall::infra::logging::LogLevel::Warn,
      .module = std::string("logging"),
      .message = std::string("invalid attrs"),
      .attrs = {{"", "bad"}},
      .ts = 1711968607001,
  });
  assert_true(!invalid_dispatch.ok,
              "sink dispatcher should reject events with non-serializable attrs");
  assert_true(invalid_dispatch.references_only_contract_error_types(),
              "sink dispatcher validation failures should stay inside contracts error types");

  const auto invalid_flush = dispatcher.flush(LogFlushDeadline{});
  assert_true(!invalid_flush.ok,
              "sink dispatcher should reject a zero timeout flush deadline");

  const auto flush_result = dispatcher.flush(LogFlushDeadline{.timeout_ms = 300});
  assert_true(flush_result.ok,
              "sink dispatcher should expose a valid flush outlet for the routed skeleton");
}

}  // namespace

int main() {
  try {
    test_sink_dispatcher_routes_runtime_events_to_basic_file_path();
    test_sink_dispatcher_routes_audit_events_without_mutating_attrs();
    test_sink_dispatcher_validates_input_and_flush_deadlines();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}