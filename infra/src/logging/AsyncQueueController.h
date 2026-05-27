#pragma once

#include <cstddef>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <mutex>
#include <string_view>
#include <thread>

#include "LoggingPipelineTypes.h"
#include "logging/ILogger.h"

namespace dasall::infra::logging {

enum class AsyncQueueOverflowPolicy {
  Unspecified = 0,
  Block,
  OverrunOldest,
};

[[nodiscard]] inline constexpr std::string_view async_queue_overflow_policy_name(
    AsyncQueueOverflowPolicy policy) {
  switch (policy) {
    case AsyncQueueOverflowPolicy::Unspecified:
      return "unspecified";
    case AsyncQueueOverflowPolicy::Block:
      return "block";
    case AsyncQueueOverflowPolicy::OverrunOldest:
      return "overrun_oldest";
  }

  return "unknown";
}

struct AsyncQueueOptions {
  std::size_t capacity = 8192;
  AsyncQueueOverflowPolicy overflow_policy = AsyncQueueOverflowPolicy::Block;

  [[nodiscard]] bool has_consistent_values() const {
    return capacity > 0U &&
           overflow_policy != AsyncQueueOverflowPolicy::Unspecified;
  }
};

struct QueueEnqueueResult {
  bool accepted = false;
  bool would_block = false;
  bool dropped_oldest = false;
  std::uint32_t queue_depth = 0;
  std::uint64_t dropped_total = 0;

  [[nodiscard]] bool has_consistent_values() const {
    if (would_block && dropped_oldest) {
      return false;
    }

    if (would_block && accepted) {
      return false;
    }

    return true;
  }
};

class AsyncQueueController {
 public:
  using RecordConsumer = std::function<LogWriteResult(const RoutedLogRecord&)>;

  explicit AsyncQueueController(AsyncQueueOptions options = {},
                                RecordConsumer consumer = {});
  ~AsyncQueueController();

  AsyncQueueController(const AsyncQueueController&) = delete;
  AsyncQueueController& operator=(const AsyncQueueController&) = delete;

  LogWriteResult start(RecordConsumer consumer);
  void stop();

  LogWriteResult enqueue(const RoutedLogRecord& record);
  LogWriteResult flush(const LogFlushDeadline& deadline);

  [[nodiscard]] const AsyncQueueOptions& options() const {
    return options_;
  }

  [[nodiscard]] QueueEnqueueResult last_enqueue_result() const {
    return last_enqueue_result_;
  }

  [[nodiscard]] bool has_pending_records() const {
    std::lock_guard lock(mutex_);
    return !queue_.empty() || in_flight_count_ > 0U;
  }

  [[nodiscard]] std::optional<RoutedLogRecord> oldest_record() const {
    std::lock_guard lock(mutex_);
    if (queue_.empty()) {
      return std::nullopt;
    }

    return queue_.front();
  }

  [[nodiscard]] std::optional<RoutedLogRecord> newest_record() const {
    std::lock_guard lock(mutex_);
    if (queue_.empty()) {
      return std::nullopt;
    }

    return queue_.back();
  }

  [[nodiscard]] std::size_t queue_depth() const {
    std::lock_guard lock(mutex_);
    return queue_.size() + in_flight_count_;
  }

  [[nodiscard]] std::uint64_t dropped_total() const {
    std::lock_guard lock(mutex_);
    return dropped_total_;
  }

  [[nodiscard]] std::uint64_t blocked_write_attempt_total() const {
    std::lock_guard lock(mutex_);
    return blocked_write_attempt_total_;
  }

  [[nodiscard]] std::uint32_t last_flush_timeout_ms() const {
    std::lock_guard lock(mutex_);
    return last_flush_timeout_ms_;
  }

  [[nodiscard]] std::uint64_t processed_total() const {
    std::lock_guard lock(mutex_);
    return processed_total_;
  }

  [[nodiscard]] std::uint64_t flush_timeout_total() const {
    std::lock_guard lock(mutex_);
    return flush_timeout_total_;
  }

  [[nodiscard]] std::uint64_t worker_failure_total() const {
    std::lock_guard lock(mutex_);
    return worker_failure_total_;
  }

  [[nodiscard]] bool worker_started() const {
    std::lock_guard lock(mutex_);
    return worker_started_;
  }

 private:
  void worker_loop();
  [[nodiscard]] bool queue_is_drained_locked() const;

  AsyncQueueOptions options_{};
  RecordConsumer consumer_;
  mutable std::mutex mutex_;
  std::condition_variable queue_cv_;
  std::condition_variable drain_cv_;
  std::deque<RoutedLogRecord> queue_;
  QueueEnqueueResult last_enqueue_result_{};
  std::uint64_t dropped_total_ = 0;
  std::uint64_t blocked_write_attempt_total_ = 0;
  std::uint32_t last_flush_timeout_ms_ = 0;
  std::uint64_t processed_total_ = 0;
  std::uint64_t flush_timeout_total_ = 0;
  std::uint64_t worker_failure_total_ = 0;
  std::size_t in_flight_count_ = 0;
  std::optional<LogWriteResult> last_worker_failure_;
  std::thread worker_thread_;
  bool worker_started_ = false;
  bool stop_requested_ = false;
};

}  // namespace dasall::infra::logging