#include <exception>
#include <iostream>
#include <string>

#include "logging/LoggingFacade.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::infra::logging::LogContext make_context() {
  return dasall::infra::logging::LogContext{
      .request_id = std::string("req-log-006"),
      .session_id = std::string("session-log-006"),
      .trace_id = std::string("trace-log-006"),
      .task_id = std::string("task-log-006"),
      .parent_task_id = std::string("parent-task-log-006"),
      .lease_id = std::string("lease-log-006"),
  };
}

dasall::infra::logging::LogEvent make_event(dasall::infra::logging::LogLevel level) {
  return dasall::infra::logging::LogEvent{
      .level = level,
      .module = std::string("runtime"),
      .message = std::string("logging facade skeleton ready"),
      .attrs = {{"component", "logging"}},
      .ts = 1711968606000,
  };
}

void test_logging_facade_rejects_log_before_init() {
  using dasall::infra::logging::LoggingFacade;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  LoggingFacade facade;

  const auto result = facade.log(make_event(dasall::infra::logging::LogLevel::Info));
  assert_true(!result.ok,
              "logging facade should reject log() before init");
  assert_true(result.references_only_contract_error_types(),
              "logging lifecycle failures should stay inside contracts error types");
  assert_equal(std::string("created"),
               std::string(facade.lifecycle_state_name()),
               "logging facade should stay in created state before init");
}

void test_logging_facade_enriches_context_on_happy_path() {
  using dasall::infra::logging::LogFlushDeadline;
  using dasall::infra::logging::LoggingFacade;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  LoggingFacade facade;

  const auto init_result = facade.init(make_context());
  assert_true(init_result.ok,
              "logging facade should initialize from created state");

  const auto log_result = facade.log(make_event(dasall::infra::logging::LogLevel::Error));
  assert_true(log_result.ok,
              "logging facade should accept a valid event after init");
  assert_true(facade.has_last_dispatched_event(),
              "logging facade should retain the last dispatched record in the skeleton path");

  const auto& dispatched = facade.last_dispatched_event();
  assert_true(dispatched.attrs.at("request_id") == "req-log-006",
              "logging facade should enrich request_id before dispatch");
  assert_true(dispatched.attrs.at("session_id") == "session-log-006",
              "logging facade should enrich session_id before dispatch");
  assert_true(dispatched.attrs.at("trace_id") == "trace-log-006",
              "logging facade should enrich trace_id before dispatch");
  assert_true(dispatched.attrs.at("task_id") == "task-log-006",
              "logging facade should enrich task_id before dispatch");
  assert_true(dispatched.attrs.at("parent_task_id") == "parent-task-log-006",
              "logging facade should enrich parent_task_id before dispatch");
  assert_true(dispatched.attrs.at("lease_id") == "lease-log-006",
              "logging facade should enrich lease_id before dispatch");
  assert_equal(1,
               static_cast<int>(facade.dispatched_record_count()),
               "logging facade should count successful dispatches on the skeleton path");

  const auto flush_result = facade.flush(LogFlushDeadline{.timeout_ms = 250});
  assert_true(flush_result.ok,
              "logging facade should expose a working flush outlet after init");
}

void test_logging_facade_validates_input_and_applies_level_filter() {
  using dasall::infra::logging::LogFlushDeadline;
  using dasall::infra::logging::LogLevel;
  using dasall::infra::logging::LoggingFacade;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  LoggingFacade facade;
  const auto init_result = facade.init(make_context());
  assert_true(init_result.ok,
              "logging facade should initialize before validation-path checks");

  facade.set_level(LogLevel::Error);
  const auto filtered_result = facade.log(make_event(LogLevel::Info));
  assert_true(filtered_result.ok,
              "logging facade should treat below-threshold events as an explicit no-op success");
  assert_equal(0,
               static_cast<int>(facade.dispatched_record_count()),
               "logging facade should not dispatch below-threshold events");

  const auto invalid_result = facade.log(dasall::infra::logging::LogEvent{
      .level = LogLevel::Error,
      .module = std::string("logging"),
      .message = std::string("invalid attrs"),
      .attrs = {{"", "bad"}},
      .ts = 1711968607000,
  });
  assert_true(!invalid_result.ok,
              "logging facade should reject non-serializable attrs after init");
  assert_true(invalid_result.references_only_contract_error_types(),
              "logging validation failures should remain inside contracts error types");

  const auto invalid_flush = facade.flush(LogFlushDeadline{});
  assert_true(!invalid_flush.ok,
              "logging facade should reject a zero timeout flush deadline");
}

}  // namespace

int main() {
  try {
    test_logging_facade_rejects_log_before_init();
    test_logging_facade_enriches_context_on_happy_path();
    test_logging_facade_validates_input_and_applies_level_filter();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}