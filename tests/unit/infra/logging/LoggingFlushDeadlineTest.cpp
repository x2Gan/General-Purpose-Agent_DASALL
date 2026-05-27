#include <condition_variable>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

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

[[nodiscard]] dasall::infra::logging::LogEvent make_event() {
  return dasall::infra::logging::LogEvent{
      .level = dasall::infra::logging::LogLevel::Info,
      .module = std::string("runtime"),
      .message = std::string("logging flush deadline deterministic"),
      .attrs = {{"component", "logging"}},
      .ts = 1711968610000,
  };
}

void test_logging_facade_flush_times_out_for_a_stuck_worker_and_succeeds_after_release() {
  using dasall::contracts::ResultCode;
  using dasall::infra::logging::LoggingFacade;
  using dasall::infra::logging::SinkDispatcher;
  using dasall::infra::logging::SinkDispatcherOptions;
  using dasall::tests::support::assert_true;

  auto blocking_sink = std::make_shared<BlockingLogSink>();
  auto dispatcher = std::make_unique<SinkDispatcher>(SinkDispatcherOptions{
      .queue_options = {},
      .basic_sink = blocking_sink,
      .audit_sink = nullptr,
  });
  LoggingFacade facade(std::move(dispatcher));

  assert_true(facade.init().ok,
              "LoggingFlushDeadlineTest should initialize the logging facade before exercising flush deadlines");
  assert_true(facade.log(make_event()).ok,
              "LoggingFlushDeadlineTest should accept a record into the worker-backed dispatcher");

  blocking_sink->wait_for_write_start();

  const auto timeout_result =
      facade.flush(dasall::infra::logging::LogFlushDeadline{.timeout_ms = 1});
  assert_true(!timeout_result.ok,
              "LoggingFlushDeadlineTest should return an explicit failure when the worker cannot drain before the deadline");
  assert_true(timeout_result.result_code == ResultCode::RuntimeRetryExhausted,
              "LoggingFlushDeadlineTest should map flush deadline timeouts to RuntimeRetryExhausted");

    const auto stop_while_blocked = facade.stop();
    assert_true(!stop_while_blocked.ok,
          "LoggingFlushDeadlineTest should keep stop deterministic by rejecting shutdown while the worker still cannot drain before the shutdown deadline");

  blocking_sink->release();

  const auto flush_result =
      facade.flush(dasall::infra::logging::LogFlushDeadline{.timeout_ms = 500});
  assert_true(flush_result.ok,
              "LoggingFlushDeadlineTest should succeed once the stuck worker is released and the queue can drain");

    const auto stop_result = facade.stop();
    assert_true(stop_result.ok,
          "LoggingFlushDeadlineTest should stop successfully once the worker has been released and the queue is drained");
}

}  // namespace

int main() {
  try {
    test_logging_facade_flush_times_out_for_a_stuck_worker_and_succeeds_after_release();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}