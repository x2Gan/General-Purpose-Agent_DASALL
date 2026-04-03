#include "AsyncQueueController.h"

#include <string>

namespace dasall::infra::logging {

namespace {

constexpr std::string_view kAsyncQueueControllerSourceRef = "AsyncQueueController";

}  // namespace

AsyncQueueController::AsyncQueueController(AsyncQueueOptions options)
    : options_(options) {}

LogWriteResult AsyncQueueController::enqueue(const RoutedLogRecord& record) {
  if (!options_.has_consistent_values()) {
    return LogWriteResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "async queue options must define a positive capacity and a concrete overflow policy",
        "logging.queue",
        std::string(kAsyncQueueControllerSourceRef));
  }

  if (!record.event.attrs_are_serializable()) {
    return LogWriteResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "async queue accepts only serializable routed records",
        "logging.queue",
        std::string(kAsyncQueueControllerSourceRef));
  }

  if (queue_.size() < options_.capacity) {
    queue_.push_back(record);
    last_enqueue_result_ = QueueEnqueueResult{
        .accepted = true,
        .would_block = false,
        .dropped_oldest = false,
        .queue_depth = static_cast<std::uint32_t>(queue_.size()),
        .dropped_total = dropped_total_,
    };
    return LogWriteResult::success();
  }

  if (options_.overflow_policy == AsyncQueueOverflowPolicy::Block) {
    ++blocked_write_attempt_total_;
    last_enqueue_result_ = QueueEnqueueResult{
        .accepted = false,
        .would_block = true,
        .dropped_oldest = false,
        .queue_depth = static_cast<std::uint32_t>(queue_.size()),
        .dropped_total = dropped_total_,
    };
    return LogWriteResult::failure(
        contracts::ResultCode::RuntimeRetryExhausted,
        "async queue is full under block policy",
        "logging.queue",
        std::string(kAsyncQueueControllerSourceRef));
  }

  queue_.pop_front();
  ++dropped_total_;
  queue_.push_back(record);
  last_enqueue_result_ = QueueEnqueueResult{
      .accepted = true,
      .would_block = false,
      .dropped_oldest = true,
      .queue_depth = static_cast<std::uint32_t>(queue_.size()),
      .dropped_total = dropped_total_,
  };
  return LogWriteResult::success();
}

LogWriteResult AsyncQueueController::flush(const LogFlushDeadline& deadline) {
  if (!deadline.is_valid()) {
    return LogWriteResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "async queue flush deadline must be greater than zero",
        "logging.flush",
        std::string(kAsyncQueueControllerSourceRef));
  }

  last_flush_timeout_ms_ = deadline.timeout_ms;
  return LogWriteResult::success();
}

}  // namespace dasall::infra::logging