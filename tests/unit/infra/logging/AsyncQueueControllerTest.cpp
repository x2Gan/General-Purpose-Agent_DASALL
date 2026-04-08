#include <exception>
#include <iostream>
#include <string>

#include "logging/AsyncQueueController.h"
#include "support/TestAssertions.h"

namespace {

dasall::infra::logging::RoutedLogRecord make_record(
    dasall::infra::logging::SinkRoute route,
    std::string message) {
  return dasall::infra::logging::RoutedLogRecord{
      .route = route,
      .event = dasall::infra::logging::LogEvent{
          .level = dasall::infra::logging::LogLevel::Info,
          .module = route == dasall::infra::logging::SinkRoute::Audit
                        ? std::string("audit")
                        : std::string("runtime"),
          .message = std::move(message),
          .attrs = {{"request_id", "req-log-008"}},
          .ts = 1711968608000,
      },
  };
}

void test_async_queue_controller_blocks_when_capacity_is_exhausted() {
  using dasall::infra::logging::AsyncQueueController;
  using dasall::infra::logging::AsyncQueueOptions;
  using dasall::infra::logging::AsyncQueueOverflowPolicy;
  using dasall::infra::logging::SinkRoute;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AsyncQueueController controller(AsyncQueueOptions{
      .capacity = 1,
      .overflow_policy = AsyncQueueOverflowPolicy::Block,
  });

  const auto first = controller.enqueue(make_record(SinkRoute::BasicFile, "first"));
  assert_true(first.ok,
              "async queue should accept the first record under block policy");

  const auto second = controller.enqueue(make_record(SinkRoute::BasicFile, "second"));
  assert_true(!second.ok,
              "async queue should return an explicit failure when block policy hits full capacity");
  assert_true(controller.last_enqueue_result().has_consistent_values(),
              "block-policy enqueue results should remain self-consistent");
  assert_true(controller.last_enqueue_result().would_block,
              "block-policy overflow should mark the would_block signal");
  assert_equal(1,
               static_cast<int>(controller.blocked_write_attempt_total()),
               "block-policy overflow should increase the blocked-write counter");
  assert_equal(1,
               static_cast<int>(controller.queue_depth()),
               "block-policy overflow should keep the queue depth bounded at capacity");
}

void test_async_queue_controller_overruns_oldest_and_tracks_drop_count() {
  using dasall::infra::logging::AsyncQueueController;
  using dasall::infra::logging::AsyncQueueOptions;
  using dasall::infra::logging::AsyncQueueOverflowPolicy;
  using dasall::infra::logging::SinkRoute;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AsyncQueueController controller(AsyncQueueOptions{
      .capacity = 1,
      .overflow_policy = AsyncQueueOverflowPolicy::OverrunOldest,
  });

  assert_true(controller.enqueue(make_record(SinkRoute::BasicFile, "oldest")).ok,
              "overrun_oldest queue should accept the first record");
  const auto second = controller.enqueue(make_record(SinkRoute::Audit, "newest"));
  assert_true(second.ok,
              "overrun_oldest queue should accept the newest record even when full");
  assert_true(controller.last_enqueue_result().dropped_oldest,
              "overrun_oldest queue should surface that it replaced the oldest record");
  assert_equal(1,
               static_cast<int>(controller.dropped_total()),
               "overrun_oldest queue should increase the drop counter when replacing the oldest record");
  assert_true(controller.newest_record().event.message == "newest",
              "overrun_oldest queue should retain the newest record after replacement");
  assert_true(controller.newest_record().route == SinkRoute::Audit,
              "overrun_oldest queue should keep the routed sink classification of the retained record");
}

void test_async_queue_controller_validates_options_and_flush_deadlines() {
  using dasall::infra::logging::AsyncQueueController;
  using dasall::infra::logging::AsyncQueueOptions;
  using dasall::infra::logging::AsyncQueueOverflowPolicy;
  using dasall::infra::logging::LogFlushDeadline;
  using dasall::infra::logging::SinkRoute;
  using dasall::tests::support::assert_true;

  AsyncQueueController invalid_controller(AsyncQueueOptions{
      .capacity = 0,
      .overflow_policy = AsyncQueueOverflowPolicy::Block,
  });
  const auto invalid_enqueue = invalid_controller.enqueue(
      make_record(SinkRoute::BasicFile, "invalid-options"));
  assert_true(!invalid_enqueue.ok,
              "async queue should reject invalid queue options before enqueueing records");
  assert_true(invalid_enqueue.references_only_contract_error_types(),
              "async queue option failures should remain inside contracts error types");

  AsyncQueueController valid_controller;
  const auto invalid_flush = valid_controller.flush(LogFlushDeadline{});
  assert_true(!invalid_flush.ok,
              "async queue should reject a zero timeout flush deadline");
  const auto flush_result = valid_controller.flush(LogFlushDeadline{.timeout_ms = 400});
  assert_true(flush_result.ok,
              "async queue should accept a positive flush deadline");
}

}  // namespace

int main() {
  try {
    test_async_queue_controller_blocks_when_capacity_is_exhausted();
    test_async_queue_controller_overruns_oldest_and_tracks_drop_count();
    test_async_queue_controller_validates_options_and_flush_deadlines();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}