#include "AsyncQueueController.h"

#include <chrono>
#include <string>
#include <utility>

namespace dasall::infra::logging {

namespace {

constexpr std::string_view kAsyncQueueControllerSourceRef = "AsyncQueueController";

[[nodiscard]] LogWriteResult make_queue_failure(std::string message,
                                                std::string stage) {
  return LogWriteResult::failure(
      contracts::ResultCode::RuntimeRetryExhausted,
      std::move(message),
      std::move(stage),
      std::string(kAsyncQueueControllerSourceRef));
}

}  // namespace

AsyncQueueController::AsyncQueueController(AsyncQueueOptions options,
                                           RecordConsumer consumer)
    : options_(options), consumer_(std::move(consumer)) {
  if (consumer_) {
    const auto start_result = start(consumer_);
    (void)start_result;
  }
}

AsyncQueueController::~AsyncQueueController() {
  stop();
}

LogWriteResult AsyncQueueController::start(RecordConsumer consumer) {
  if (!options_.has_consistent_values()) {
    return LogWriteResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "async queue options must define a positive capacity and a concrete overflow policy",
        "logging.queue",
        std::string(kAsyncQueueControllerSourceRef));
  }

  if (!consumer) {
    return LogWriteResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "async queue worker requires a concrete record consumer",
        "logging.queue.start",
        std::string(kAsyncQueueControllerSourceRef));
  }

  std::lock_guard lock(mutex_);
  if (worker_started_) {
    return LogWriteResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "async queue worker has already been started",
        "logging.queue.start",
        std::string(kAsyncQueueControllerSourceRef));
  }

  consumer_ = std::move(consumer);
  stop_requested_ = false;
  last_worker_failure_.reset();
  worker_started_ = true;
  worker_thread_ = std::thread([this]() { worker_loop(); });
  return LogWriteResult::success();
}

void AsyncQueueController::stop() {
  {
    std::lock_guard lock(mutex_);
    if (!worker_started_) {
      return;
    }

    stop_requested_ = true;
  }

  queue_cv_.notify_all();
  drain_cv_.notify_all();
  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }

  std::lock_guard lock(mutex_);
  worker_started_ = false;
  consumer_ = {};
}

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

  std::lock_guard lock(mutex_);
  const auto occupancy = queue_.size() + in_flight_count_;
  if (occupancy < options_.capacity) {
    queue_.push_back(record);
    last_enqueue_result_ = QueueEnqueueResult{
        .accepted = true,
        .would_block = false,
        .dropped_oldest = false,
        .queue_depth = static_cast<std::uint32_t>(queue_.size() + in_flight_count_),
        .dropped_total = dropped_total_,
    };
    queue_cv_.notify_one();
    return LogWriteResult::success();
  }

  if (options_.overflow_policy == AsyncQueueOverflowPolicy::Block || queue_.empty()) {
    ++blocked_write_attempt_total_;
    last_enqueue_result_ = QueueEnqueueResult{
        .accepted = false,
        .would_block = true,
        .dropped_oldest = false,
        .queue_depth = static_cast<std::uint32_t>(queue_.size() + in_flight_count_),
        .dropped_total = dropped_total_,
    };
    return make_queue_failure(
        "async queue is full under the current overflow policy",
        "logging.queue");
  }

  queue_.pop_front();
  ++dropped_total_;
  queue_.push_back(record);
  last_enqueue_result_ = QueueEnqueueResult{
      .accepted = true,
      .would_block = false,
      .dropped_oldest = true,
      .queue_depth = static_cast<std::uint32_t>(queue_.size() + in_flight_count_),
      .dropped_total = dropped_total_,
  };
    queue_cv_.notify_one();
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

  std::unique_lock lock(mutex_);
  last_flush_timeout_ms_ = deadline.timeout_ms;
  if (!worker_started_) {
    return LogWriteResult::success();
  }

  const auto deadline_time =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(deadline.timeout_ms);
  const auto drained = drain_cv_.wait_until(lock, deadline_time, [this]() {
    return queue_is_drained_locked() || last_worker_failure_.has_value();
  });

  if (!drained) {
    ++flush_timeout_total_;
    return make_queue_failure(
        "async queue flush timed out before the worker drained all pending records",
        "logging.flush");
  }

  if (last_worker_failure_.has_value()) {
    return *last_worker_failure_;
  }

  return LogWriteResult::success();
}

void AsyncQueueController::worker_loop() {
  while (true) {
    std::optional<RoutedLogRecord> record;
    RecordConsumer consumer;
    {
      std::unique_lock lock(mutex_);
      queue_cv_.wait(lock, [this]() {
        return stop_requested_ || !queue_.empty();
      });

      if (queue_.empty()) {
        if (stop_requested_) {
          break;
        }

        continue;
      }

      record = queue_.front();
      queue_.pop_front();
      ++in_flight_count_;
      consumer = consumer_;
    }

    const auto result = consumer ? consumer(*record) : LogWriteResult::success();

    {
      std::lock_guard lock(mutex_);
      --in_flight_count_;
      if (result.ok) {
        ++processed_total_;
      } else {
        ++worker_failure_total_;
        last_worker_failure_ = result;
      }
    }

    drain_cv_.notify_all();
    queue_cv_.notify_all();
  }

  drain_cv_.notify_all();
}

bool AsyncQueueController::queue_is_drained_locked() const {
  return queue_.empty() && in_flight_count_ == 0U;
}

}  // namespace dasall::infra::logging