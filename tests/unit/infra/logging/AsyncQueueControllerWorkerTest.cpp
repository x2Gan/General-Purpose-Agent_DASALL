#include <exception>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

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
          .attrs = {{"request_id", "req-log-async-worker-004"}},
          .ts = 1711968609000,
      },
  };
}

void test_async_queue_worker_drains_records_and_flushes_deterministically() {
  using dasall::infra::logging::AsyncQueueController;
  using dasall::infra::logging::AsyncQueueOptions;
  using dasall::infra::logging::AsyncQueueOverflowPolicy;
  using dasall::infra::logging::LogFlushDeadline;
  using dasall::infra::logging::SinkRoute;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  std::mutex processed_mutex;
  std::vector<std::string> processed_messages;
  AsyncQueueController controller(AsyncQueueOptions{
      .capacity = 4,
      .overflow_policy = AsyncQueueOverflowPolicy::Block,
  });

  const auto start_result = controller.start(
      [&processed_mutex, &processed_messages](
          const dasall::infra::logging::RoutedLogRecord& record) {
        std::lock_guard lock(processed_mutex);
        processed_messages.push_back(record.event.message);
        return dasall::infra::logging::LogWriteResult::success();
      });
  assert_true(start_result.ok,
              "AsyncQueueControllerWorkerTest should start a worker when a concrete consumer is supplied");
  assert_true(controller.worker_started(),
              "AsyncQueueControllerWorkerTest should report that the worker is active after start");

  assert_true(controller.enqueue(make_record(SinkRoute::BasicFile, "first")).ok,
              "AsyncQueueControllerWorkerTest should accept the first queued record");
  assert_true(controller.enqueue(make_record(SinkRoute::Audit, "second")).ok,
              "AsyncQueueControllerWorkerTest should accept the second queued record");

  const auto flush_result = controller.flush(LogFlushDeadline{.timeout_ms = 500});
  assert_true(flush_result.ok,
              "AsyncQueueControllerWorkerTest should flush successfully once the worker drains the queue");
  assert_equal(0,
               static_cast<int>(controller.queue_depth()),
               "AsyncQueueControllerWorkerTest should leave no pending records after a successful flush");
  assert_equal(2,
               static_cast<int>(controller.processed_total()),
               "AsyncQueueControllerWorkerTest should increase the processed counter for each drained record");

  std::lock_guard lock(processed_mutex);
  assert_equal(2,
               static_cast<int>(processed_messages.size()),
               "AsyncQueueControllerWorkerTest should pass both records through the worker consumer");
  assert_true(processed_messages.front() == "first" &&
                  processed_messages.back() == "second",
              "AsyncQueueControllerWorkerTest should preserve queue ordering through the single worker");
}

void test_async_queue_worker_stop_drains_pending_records_before_joining() {
  using dasall::infra::logging::AsyncQueueController;
  using dasall::infra::logging::AsyncQueueOptions;
  using dasall::infra::logging::AsyncQueueOverflowPolicy;
  using dasall::infra::logging::SinkRoute;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  std::mutex processed_mutex;
  std::vector<std::string> processed_messages;
  AsyncQueueController controller(AsyncQueueOptions{
      .capacity = 2,
      .overflow_policy = AsyncQueueOverflowPolicy::Block,
  });

  assert_true(controller.start(
                  [&processed_mutex, &processed_messages](
                      const dasall::infra::logging::RoutedLogRecord& record) {
                    std::lock_guard lock(processed_mutex);
                    processed_messages.push_back(record.event.message);
                    return dasall::infra::logging::LogWriteResult::success();
                  })
                  .ok,
              "AsyncQueueControllerWorkerTest should start the worker before testing stop semantics");
  assert_true(controller.enqueue(make_record(SinkRoute::BasicFile, "shutdown-drain")).ok,
              "AsyncQueueControllerWorkerTest should accept a queued record before stop");

  controller.stop();

  assert_true(!controller.worker_started(),
              "AsyncQueueControllerWorkerTest should report an inactive worker after stop joins the worker thread");
  assert_equal(0,
               static_cast<int>(controller.queue_depth()),
               "AsyncQueueControllerWorkerTest should drain pending records before stop returns");
  assert_equal(1,
               static_cast<int>(controller.processed_total()),
               "AsyncQueueControllerWorkerTest should count the drained record before shutdown completes");

  std::lock_guard lock(processed_mutex);
  assert_equal(1,
               static_cast<int>(processed_messages.size()),
               "AsyncQueueControllerWorkerTest should deliver the pending record before the worker stops");
}

}  // namespace

int main() {
  try {
    test_async_queue_worker_drains_records_and_flushes_deterministically();
    test_async_queue_worker_stop_drains_pending_records_before_joining();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}