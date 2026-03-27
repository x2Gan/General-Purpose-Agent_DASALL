#include "linux/BlockingQueueProvider.h"

#include <optional>
#include <utility>

namespace dasall::platform::linux {

PlatformResult<QueueHandle> BlockingQueueProvider::create_queue(const QueueOptions& options) {
  if (!options.has_consistent_values()) {
    return PlatformResult<QueueHandle>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "queue options are invalid"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const std::uint64_t id = next_id_++;
  queues_.emplace(id,
                  QueueState{
                      .options = options,
                      .closed = false,
                      .items = {},
                  });

  return PlatformResult<QueueHandle>::success(QueueHandle{.native_id = id});
}

PlatformResult<QueuePushResult> BlockingQueueProvider::push(const QueueHandle& handle,
                                                            const QueueItem& item,
                                                            std::int32_t timeout_ms) {
  if (!handle.has_consistent_values() || timeout_ms < 0) {
    return PlatformResult<QueuePushResult>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "queue handle or timeout is invalid"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = queues_.find(handle.native_id);
  if (it == queues_.end()) {
    return PlatformResult<QueuePushResult>::failure(
        make_error(PlatformErrorCode::NotFound,
                   PlatformErrorCategory::Resource,
                   "queue handle does not exist"));
  }

  if (it->second.closed) {
    return PlatformResult<QueuePushResult>::failure(
        make_error(PlatformErrorCode::QueueClosed,
                   PlatformErrorCategory::Resource,
                   "queue has been closed"));
  }

  if (it->second.items.size() >= it->second.options.capacity) {
    if (timeout_ms == 0) {
      return PlatformResult<QueuePushResult>::failure(
          make_error(PlatformErrorCode::ResourceExhausted,
                     PlatformErrorCategory::Resource,
                     "queue is full and push timed out"));
    }

    return PlatformResult<QueuePushResult>::failure(
        make_error(PlatformErrorCode::Timeout,
                   PlatformErrorCategory::Resource,
                   "queue is full before timeout deadline"));
  }

  it->second.items.push_back(item);
  return PlatformResult<QueuePushResult>::success(QueuePushResult{
      .enqueued = true,
      .queue_depth = static_cast<std::uint32_t>(it->second.items.size()),
  });
}

PlatformResult<QueuePopResult> BlockingQueueProvider::pop(const QueueHandle& handle,
                                                          std::int32_t timeout_ms) {
  if (!handle.has_consistent_values() || timeout_ms < 0) {
    return PlatformResult<QueuePopResult>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "queue handle or timeout is invalid"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = queues_.find(handle.native_id);
  if (it == queues_.end()) {
    return PlatformResult<QueuePopResult>::failure(
        make_error(PlatformErrorCode::NotFound,
                   PlatformErrorCategory::Resource,
                   "queue handle does not exist"));
  }

  if (it->second.items.empty()) {
    if (timeout_ms == 0) {
      return PlatformResult<QueuePopResult>::failure(
          make_error(PlatformErrorCode::Timeout,
                     PlatformErrorCategory::Resource,
                     "queue pop timed out with no available item"));
    }

    return PlatformResult<QueuePopResult>::success(QueuePopResult{
        .has_item = false,
        .item = {},
        .queue_depth = 0,
    });
  }

  QueueItem item = std::move(it->second.items.front());
  it->second.items.pop_front();

  return PlatformResult<QueuePopResult>::success(QueuePopResult{
      .has_item = true,
      .item = std::move(item),
      .queue_depth = static_cast<std::uint32_t>(it->second.items.size()),
  });
}

PlatformResult<QueueCloseResult> BlockingQueueProvider::close(const QueueHandle& handle) {
  if (!handle.has_consistent_values()) {
    return PlatformResult<QueueCloseResult>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "queue handle is invalid"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = queues_.find(handle.native_id);
  if (it == queues_.end()) {
    return PlatformResult<QueueCloseResult>::failure(
        make_error(PlatformErrorCode::NotFound,
                   PlatformErrorCategory::Resource,
                   "queue handle does not exist"));
  }

  if (it->second.closed) {
    return PlatformResult<QueueCloseResult>::success(QueueCloseResult{
        .already_closed = true,
        .dropped_items = 0,
    });
  }

  it->second.closed = true;
  std::uint32_t dropped = 0;
  if (it->second.options.shutdown_policy == QueueShutdownPolicy::DropPending) {
    dropped = static_cast<std::uint32_t>(it->second.items.size());
    it->second.items.clear();
  }

  return PlatformResult<QueueCloseResult>::success(QueueCloseResult{
      .already_closed = false,
      .dropped_items = dropped,
  });
}

PlatformError BlockingQueueProvider::make_error(PlatformErrorCode code,
                                                PlatformErrorCategory category,
                                                std::string detail) const {
  return PlatformError{
      .code = code,
      .category = category,
      .retryable_hint = (code == PlatformErrorCode::Timeout),
      .syscall_name = {},
      .errno_value = std::nullopt,
      .detail = std::move(detail),
  };
}

}  // namespace dasall::platform::linux