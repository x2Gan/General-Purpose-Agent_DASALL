#include "tracing/BatchSpanBuffer.h"

#include <algorithm>
#include <string>
#include <utility>

#include "tracing/TraceErrors.h"

namespace dasall::infra::tracing {
namespace {

constexpr std::string_view kBatchSpanBufferSourceRef = "BatchSpanBuffer";

}  // namespace

BatchSpanBuffer::BatchSpanBuffer(TraceConfig config)
    : config_(std::move(config)) {}

BatchSpanEnqueueResult BatchSpanBuffer::enqueue(const std::shared_ptr<SpanImpl>& span) {
  if (!config_.is_valid() || !config_.batch.is_valid()) {
    last_enqueue_result_ = BatchSpanEnqueueResult{
        .status = make_failure(TraceErrorCode::ConfigInvalid,
                               "trace batch buffer requires a valid TraceConfig batch section",
                               "tracing.batch.enqueue"),
        .accepted = false,
        .would_block = false,
        .dropped_oldest = false,
        .export_requested = false,
        .queue_depth = static_cast<std::uint32_t>(queue_.size()),
        .dropped_total = dropped_total_,
    };
    return last_enqueue_result_;
  }

  if (!span || !span->has_ended() || !span->is_recording()) {
    last_enqueue_result_ = BatchSpanEnqueueResult{
        .status = make_validation_failure(
            "trace batch buffer accepts only ended recording spans",
            "tracing.batch.enqueue"),
        .accepted = false,
        .would_block = false,
        .dropped_oldest = false,
        .export_requested = false,
        .queue_depth = static_cast<std::uint32_t>(queue_.size()),
        .dropped_total = dropped_total_,
    };
    return last_enqueue_result_;
  }

  if (queue_.size() >= config_.batch.max_queue_size) {
    if (config_.overflow_policy == kTraceOverflowPolicyBlock) {
      ++blocked_enqueue_total_;
      last_enqueue_result_ = BatchSpanEnqueueResult{
          .status = make_failure(TraceErrorCode::QueueFull,
                                 "trace batch queue is full under block policy",
                                 "tracing.batch.enqueue"),
          .accepted = false,
          .would_block = true,
          .dropped_oldest = false,
          .export_requested = false,
          .queue_depth = static_cast<std::uint32_t>(queue_.size()),
          .dropped_total = dropped_total_,
      };
      return last_enqueue_result_;
    }

    queue_.pop_front();
    ++dropped_total_;
  }

  queue_.push_back(span);
  refresh_oldest_pending_end_ts();

  last_enqueue_result_ = BatchSpanEnqueueResult{
      .status = TraceOperationStatus::success("trace-buffer://enqueued"),
      .accepted = true,
      .would_block = false,
      .dropped_oldest = dropped_total_ > last_enqueue_result_.dropped_total,
      .export_requested = queue_.size() >= config_.batch.max_export_batch_size,
      .queue_depth = static_cast<std::uint32_t>(queue_.size()),
      .dropped_total = dropped_total_,
  };
  return last_enqueue_result_;
}

std::vector<std::shared_ptr<SpanImpl>> BatchSpanBuffer::dequeue_batch(std::size_t limit) {
  const auto batch_limit = limit == 0U ? default_batch_limit() : limit;
  const auto dequeue_count = std::min(batch_limit, queue_.size());

  std::vector<std::shared_ptr<SpanImpl>> batch;
  batch.reserve(dequeue_count);
  for (std::size_t index = 0; index < dequeue_count; ++index) {
    batch.push_back(queue_.front());
    queue_.pop_front();
  }

  refresh_oldest_pending_end_ts();
  return batch;
}

std::vector<std::shared_ptr<SpanImpl>> BatchSpanBuffer::force_flush() {
  return dequeue_batch(queue_.size());
}

BatchSpanBufferTrigger BatchSpanBuffer::export_trigger(std::int64_t now_unix_ms) const {
  if (queue_.empty()) {
    return BatchSpanBufferTrigger::None;
  }

  if (!config_.batch.enabled) {
    return BatchSpanBufferTrigger::QueueThreshold;
  }

  if (queue_.size() >= config_.batch.max_export_batch_size) {
    return BatchSpanBufferTrigger::QueueThreshold;
  }

  if (oldest_pending_end_ts_.has_value() &&
      now_unix_ms - *oldest_pending_end_ts_ >=
          static_cast<std::int64_t>(config_.batch.schedule_delay_ms)) {
    return BatchSpanBufferTrigger::ScheduleDelay;
  }

  return BatchSpanBufferTrigger::None;
}

bool BatchSpanBuffer::should_export_now(std::int64_t now_unix_ms) const {
  return export_trigger(now_unix_ms) != BatchSpanBufferTrigger::None;
}

void BatchSpanBuffer::mark_export_cycle_complete(std::int64_t completed_at_unix_ms) {
  last_export_completed_ts_ = completed_at_unix_ms;
}

const BatchSpanEnqueueResult& BatchSpanBuffer::last_enqueue_result() const {
  return last_enqueue_result_;
}

std::size_t BatchSpanBuffer::queue_depth() const {
  return queue_.size();
}

std::uint64_t BatchSpanBuffer::dropped_total() const {
  return dropped_total_;
}

std::uint64_t BatchSpanBuffer::blocked_enqueue_total() const {
  return blocked_enqueue_total_;
}

const std::optional<std::int64_t>& BatchSpanBuffer::oldest_pending_end_ts() const {
  return oldest_pending_end_ts_;
}

const std::optional<std::int64_t>& BatchSpanBuffer::last_export_completed_ts() const {
  return last_export_completed_ts_;
}

TraceOperationStatus BatchSpanBuffer::make_failure(TraceErrorCode code,
                                                   std::string message,
                                                   std::string stage) {
  const auto mapping = map_trace_error_code(code);
  return TraceOperationStatus::failure(mapping.result_code,
                                       std::move(message),
                                       std::move(stage),
                                       std::string(kBatchSpanBufferSourceRef) + ":" +
                                           std::string(trace_error_code_name(code)));
}

TraceOperationStatus BatchSpanBuffer::make_validation_failure(std::string message,
                                                              std::string stage) {
  return TraceOperationStatus::failure(contracts::ResultCode::ValidationFieldMissing,
                                       std::move(message),
                                       std::move(stage),
                                       std::string(kBatchSpanBufferSourceRef) +
                                           ":ValidationFieldMissing");
}

void BatchSpanBuffer::refresh_oldest_pending_end_ts() {
  if (queue_.empty()) {
    oldest_pending_end_ts_.reset();
    return;
  }

  oldest_pending_end_ts_ = queue_.front()->end_result().end_ts_unix_ms;
}

std::size_t BatchSpanBuffer::default_batch_limit() const {
  return config_.batch.enabled ? config_.batch.max_export_batch_size : queue_.size();
}

}  // namespace dasall::infra::tracing