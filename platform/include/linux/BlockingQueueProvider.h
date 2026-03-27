#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_map>

#include "IQueue.h"

namespace dasall::platform::linux {

class BlockingQueueProvider final : public IQueue {
 public:
  BlockingQueueProvider() = default;

  PlatformResult<QueueHandle> create_queue(const QueueOptions& options) override;
  PlatformResult<QueuePushResult> push(const QueueHandle& handle,
                                       const QueueItem& item,
                                       std::int32_t timeout_ms) override;
  PlatformResult<QueuePopResult> pop(const QueueHandle& handle,
                                     std::int32_t timeout_ms) override;
  PlatformResult<QueueCloseResult> close(const QueueHandle& handle) override;

 private:
  struct QueueState {
    QueueOptions options;
    bool closed = false;
    std::deque<QueueItem> items;
  };

  [[nodiscard]] PlatformError make_error(PlatformErrorCode code,
                                         PlatformErrorCategory category,
                                         std::string detail) const;

  mutable std::mutex mutex_;
  std::uint64_t next_id_ = 1;
  std::unordered_map<std::uint64_t, QueueState> queues_;
};

}  // namespace dasall::platform::linux