#include <condition_variable>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

#include "error/ResultCode.h"
#include "logging/ILogger.h"
#include "logging/LoggingFacade.h"
#include "logging/SinkDispatcher.h"
#include "support/TestAssertions.h"

namespace {

class BlockingLogSink final : public dasall::infra::logging::ILogSink {
 public:
  dasall::infra::logging::LogWriteResult write(
      const dasall::infra::logging::LogEvent&) override {
    std::unique_lock lock(mutex_);
    write_started_ = true;
    started_cv_.notify_all();
    release_cv_.wait(lock, [this]() { return released_; });
    return dasall::infra::logging::LogWriteResult::success();
  }

  dasall::infra::logging::LogWriteResult flush(
      const dasall::infra::logging::LogFlushDeadline&) override {
    return dasall::infra::logging::LogWriteResult::success();
  }

  void wait_for_write_start() {
    std::unique_lock lock(mutex_);
    started_cv_.wait(lock, [this]() { return write_started_; });
  }

  void release() {
    std::lock_guard lock(mutex_);
    released_ = true;
    release_cv_.notify_all();
  }

 private:
  std::mutex mutex_;
  std::condition_variable started_cv_;
  std::condition_variable release_cv_;
  bool write_started_ = false;
  bool released_ = false;
};

[[nodiscard]] dasall::infra::logging::LogEvent make_event(std::string sequence) {
  return dasall::infra::logging::LogEvent{
      .level = dasall::infra::logging::LogLevel::Info,
      .module = std::string("runtime"),
      .message = std::string("logging backpressure ") + sequence,
      .attrs = {{"sequence", std::move(sequence)}},
      .ts = 1711968611000,
  };
}

void test_logging_pipeline_surfaces_block_policy_backpressure_while_worker_is_busy() {
  using dasall::contracts::ResultCode;
  using dasall::infra::logging::AsyncQueueOptions;
  using dasall::infra::logging::AsyncQueueOverflowPolicy;
  using dasall::infra::logging::LoggingFacade;
  using dasall::infra::logging::SinkDispatcher;
  using dasall::infra::logging::SinkDispatcherOptions;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto blocking_sink = std::make_shared<BlockingLogSink>();
  auto dispatcher = std::make_unique<SinkDispatcher>(SinkDispatcherOptions{
      .queue_options = AsyncQueueOptions{
          .capacity = 1,
          .overflow_policy = AsyncQueueOverflowPolicy::Block,
      },
      .basic_sink = blocking_sink,
      .audit_sink = nullptr,
  });
  auto* dispatcher_ptr = dispatcher.get();
  LoggingFacade facade(std::move(dispatcher));

  assert_true(facade.init().ok,
              "LoggingBackpressureTest should initialize the facade before exercising block-policy backpressure");
  const auto first_result = facade.log(make_event("1"));
  assert_true(first_result.ok,
              "LoggingBackpressureTest should accept the first record while capacity is available");

  blocking_sink->wait_for_write_start();

  const auto second_result = facade.log(make_event("2"));
  assert_true(!second_result.ok,
              "LoggingBackpressureTest should reject the second record once the single worker slot is occupied under block policy");
  assert_true(second_result.result_code == ResultCode::RuntimeRetryExhausted,
              "LoggingBackpressureTest should map worker-side backpressure to RuntimeRetryExhausted");
  assert_equal(1,
               static_cast<int>(dispatcher_ptr->blocked_write_attempt_total()),
               "LoggingBackpressureTest should increase the blocked-write counter when the worker is busy and capacity is exhausted");
  assert_equal(1,
               static_cast<int>(dispatcher_ptr->queue_depth()),
               "LoggingBackpressureTest should keep exactly one in-flight record while the worker is stalled");

  blocking_sink->release();
  assert_true(
      facade.flush(dasall::infra::logging::LogFlushDeadline{.timeout_ms = 500}).ok,
      "LoggingBackpressureTest should drain the accepted record once the blocking sink is released");
}

}  // namespace

int main() {
  try {
    test_logging_pipeline_surfaces_block_policy_backpressure_while_worker_is_busy();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}