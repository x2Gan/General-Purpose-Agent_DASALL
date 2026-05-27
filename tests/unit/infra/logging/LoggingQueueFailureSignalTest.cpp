#include <condition_variable>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "logging/FileLogSink.h"
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

class CapturingRecoverySink final : public dasall::infra::logging::ILogRecoverySink {
 public:
  dasall::infra::logging::LogWriteResult write(
      const dasall::infra::logging::LogEvent& event) override {
    written_events_.push_back(event);
    return dasall::infra::logging::LogWriteResult::success();
  }

  [[nodiscard]] const std::vector<dasall::infra::logging::LogEvent>& written_events() const {
    return written_events_;
  }

 private:
  std::vector<dasall::infra::logging::LogEvent> written_events_;
};

[[nodiscard]] dasall::infra::logging::LogEvent make_event(std::string request_id,
                                                          std::string message) {
  return dasall::infra::logging::LogEvent{
      .level = dasall::infra::logging::LogLevel::Error,
      .module = std::string("runtime"),
      .message = std::move(message),
      .attrs = {{"request_id", std::move(request_id)}},
      .ts = 1712300401001,
  };
}

void test_logging_facade_emits_degraded_queue_saturation_signal_when_capacity_is_exhausted() {
  using dasall::infra::logging::AsyncQueueOptions;
  using dasall::infra::logging::AsyncQueueOverflowPolicy;
  using dasall::infra::logging::LogFlushDeadline;
  using dasall::infra::logging::LoggingErrorCode;
  using dasall::infra::logging::LoggingFacade;
  using dasall::infra::logging::SinkDispatcher;
  using dasall::infra::logging::SinkDispatcherOptions;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto blocking_sink = std::make_shared<BlockingLogSink>();
  auto fallback_sink = std::make_shared<CapturingRecoverySink>();
  auto dispatcher = std::make_unique<SinkDispatcher>(SinkDispatcherOptions{
      .queue_options = AsyncQueueOptions{
          .capacity = 1U,
          .overflow_policy = AsyncQueueOverflowPolicy::Block,
      },
      .basic_sink = blocking_sink,
      .audit_sink = nullptr,
  });
  LoggingFacade facade(std::move(dispatcher), fallback_sink);

  assert_true(facade.init().ok,
              "LoggingQueueFailureSignalTest should initialize the logging facade before exercising queue saturation");
  assert_true(facade.log(make_event("req-queue-001", "primary queue write")).ok,
              "LoggingQueueFailureSignalTest should accept the first record into the deterministic queue");

  blocking_sink->wait_for_write_start();

  const auto saturation_result = facade.log(
      make_event("req-queue-002", "queue saturation advisory"));
  assert_true(saturation_result.ok,
              "LoggingQueueFailureSignalTest should degrade to a fallback advisory signal when the queue is saturated");
  assert_true(facade.is_degraded() && facade.fallback_active(),
              "LoggingQueueFailureSignalTest should leave the facade in degraded mode after queue saturation");
  assert_true(facade.last_recovery_error_code() == LoggingErrorCode::QueueFull,
              "LoggingQueueFailureSignalTest should surface LOG_E_QUEUE_FULL after saturation");
  assert_equal(1,
               static_cast<int>(fallback_sink->written_events().size()),
               "LoggingQueueFailureSignalTest should emit exactly one degraded fallback advisory record");
  assert_true(fallback_sink->written_events().front().message.find("queue saturation") !=
                  std::string::npos,
              "LoggingQueueFailureSignalTest should emit a readable queue saturation advisory payload");
  assert_true(fallback_sink->written_events().front().attrs.at("logging_error_code") ==
                  "LOG_E_QUEUE_FULL",
              "LoggingQueueFailureSignalTest should attach the frozen queue-full error code to the advisory payload");

  blocking_sink->release();

  assert_true(facade.flush(LogFlushDeadline{.timeout_ms = 500}).ok,
              "LoggingQueueFailureSignalTest should still drain the original in-flight queue record once the sink is released");
  assert_true(facade.stop().ok,
              "LoggingQueueFailureSignalTest should stop cleanly after the blocked primary sink is released");
}

}  // namespace

int main() {
  try {
    test_logging_facade_emits_degraded_queue_saturation_signal_when_capacity_is_exhausted();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}