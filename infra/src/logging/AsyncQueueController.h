#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string_view>

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
  explicit AsyncQueueController(AsyncQueueOptions options = {});

  LogWriteResult enqueue(const RoutedLogRecord& record);
  LogWriteResult flush(const LogFlushDeadline& deadline);

  [[nodiscard]] const AsyncQueueOptions& options() const {
    return options_;
  }

  [[nodiscard]] const QueueEnqueueResult& last_enqueue_result() const {
    return last_enqueue_result_;
  }

  [[nodiscard]] bool has_pending_records() const {
    return !queue_.empty();
  }

  [[nodiscard]] const RoutedLogRecord& oldest_record() const {
    return queue_.front();
  }

  [[nodiscard]] const RoutedLogRecord& newest_record() const {
    return queue_.back();
  }

  [[nodiscard]] std::size_t queue_depth() const {
    return queue_.size();
  }

  [[nodiscard]] std::uint64_t dropped_total() const {
    return dropped_total_;
  }

  [[nodiscard]] std::uint64_t blocked_write_attempt_total() const {
    return blocked_write_attempt_total_;
  }

  [[nodiscard]] std::uint32_t last_flush_timeout_ms() const {
    return last_flush_timeout_ms_;
  }

 private:
  AsyncQueueOptions options_{};
  std::deque<RoutedLogRecord> queue_;
  QueueEnqueueResult last_enqueue_result_{};
  std::uint64_t dropped_total_ = 0;
  std::uint64_t blocked_write_attempt_total_ = 0;
  std::uint32_t last_flush_timeout_ms_ = 0;
};

}  // namespace dasall::infra::logging