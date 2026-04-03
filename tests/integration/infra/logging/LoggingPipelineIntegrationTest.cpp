#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "logging/AsyncQueueController.h"
#include "logging/LoggingFacade.h"
#include "logging/SinkDispatcher.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_logging_pipeline_enriches_context_and_routes_basic_records() {
  using dasall::infra::InfraContext;
  using dasall::infra::logging::AsyncQueueOptions;
  using dasall::infra::logging::AsyncQueueOverflowPolicy;
  using dasall::infra::logging::LogEvent;
  using dasall::infra::logging::LogLevel;
  using dasall::infra::logging::LoggingFacade;
  using dasall::infra::logging::SinkDispatcher;
  using dasall::infra::logging::SinkRoute;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto dispatcher = std::make_unique<SinkDispatcher>(AsyncQueueOptions{
      .capacity = 4,
      .overflow_policy = AsyncQueueOverflowPolicy::Block,
  });
  auto* dispatcher_ptr = dispatcher.get();
  LoggingFacade facade(std::move(dispatcher));

  const auto init_result = facade.init(InfraContext{
      .request_id = std::string("req-int-logging-001"),
      .session_id = std::string("session-int-logging-001"),
      .trace_id = std::string("trace-int-logging-001"),
      .task_id = std::string("task-int-logging-001"),
      .parent_task_id = std::string("parent-int-logging-001"),
      .lease_id = std::string("lease-int-logging-001"),
  });
  assert_true(init_result.ok,
              "logging pipeline integration should initialize the facade with a concrete context");

  const auto log_result = facade.log(LogEvent{
      .level = LogLevel::Info,
      .module = std::string("runtime"),
      .message = std::string("logging integration basic route"),
      .attrs = {{"event_kind", "integration_smoke"}},
      .ts = 1712217600001,
  });

  assert_true(log_result.ok,
              "logging pipeline integration should write a basic routed record successfully");
  assert_equal(1,
               static_cast<int>(facade.dispatched_record_count()),
               "logging pipeline integration should count exactly one dispatched record");
  assert_true(dispatcher_ptr->last_route() == SinkRoute::BasicFile,
              "logging pipeline integration should keep non-audit records on the basic route");
  assert_equal(1,
               static_cast<int>(dispatcher_ptr->queue_depth()),
               "logging pipeline integration should enqueue the routed record once");
  assert_true(facade.last_dispatched_event().attrs.at("trace_id") ==
                  "trace-int-logging-001",
              "logging pipeline integration should enrich the event with trace_id from context");
  assert_true(dispatcher_ptr->last_record().event.attrs.at("lease_id") ==
                  "lease-int-logging-001",
              "logging pipeline integration should preserve enriched attrs through the dispatcher");
}

void test_logging_pipeline_surfaces_queue_backpressure_without_partial_side_effects() {
  using dasall::contracts::ResultCode;
  using dasall::infra::logging::AsyncQueueOptions;
  using dasall::infra::logging::AsyncQueueOverflowPolicy;
  using dasall::infra::logging::LogEvent;
  using dasall::infra::logging::LogLevel;
  using dasall::infra::logging::LoggingFacade;
  using dasall::infra::logging::SinkDispatcher;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto dispatcher = std::make_unique<SinkDispatcher>(AsyncQueueOptions{
      .capacity = 1,
      .overflow_policy = AsyncQueueOverflowPolicy::Block,
  });
  auto* dispatcher_ptr = dispatcher.get();
  LoggingFacade facade(std::move(dispatcher));

  assert_true(facade.init().ok,
              "logging pipeline integration should initialize before exercising backpressure");

  const auto first = facade.log(LogEvent{
      .level = LogLevel::Info,
      .module = std::string("runtime"),
      .message = std::string("queue fill first record"),
      .attrs = {{"sequence", "1"}},
      .ts = 1712217600002,
  });
  const auto second = facade.log(LogEvent{
      .level = LogLevel::Info,
      .module = std::string("runtime"),
      .message = std::string("queue fill second record"),
      .attrs = {{"sequence", "2"}},
      .ts = 1712217600003,
  });

  assert_true(first.ok,
              "logging pipeline integration should accept the first record before the queue is full");
  assert_true(!second.ok,
              "logging pipeline integration should surface block-policy backpressure on the second record");
  assert_true(second.references_only_contract_error_types(),
              "logging pipeline backpressure should stay within contracts error types");
  assert_true(second.result_code == ResultCode::RuntimeRetryExhausted,
              "logging pipeline integration should map queue backpressure to RuntimeRetryExhausted");
  assert_equal(1,
               static_cast<int>(facade.dispatched_record_count()),
               "logging pipeline integration should not count the rejected record as dispatched");
  assert_equal(1,
               static_cast<int>(dispatcher_ptr->blocked_write_attempt_total()),
               "logging pipeline integration should expose one blocked write attempt");
  assert_equal(1,
               static_cast<int>(dispatcher_ptr->queue_depth()),
               "logging pipeline integration should leave the queue depth unchanged after rejection");
}

}  // namespace

int main() {
  try {
    test_logging_pipeline_enriches_context_and_routes_basic_records();
    test_logging_pipeline_surfaces_queue_backpressure_without_partial_side_effects();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}