#pragma once

#include <cstdint>
#include <vector>

#include "PlatformResult.h"

namespace dasall::platform {

enum class QueueOverflowPolicy {
  Reject,
  Block,
};

enum class QueueShutdownPolicy {
  Drain,
  DropPending,
};

struct QueueOptions {
  std::uint32_t capacity = 1024;
  QueueOverflowPolicy overflow_policy = QueueOverflowPolicy::Reject;
  QueueShutdownPolicy shutdown_policy = QueueShutdownPolicy::Drain;

  [[nodiscard]] bool has_consistent_values() const {
    return capacity != 0U;
  }
};

using QueueItem = std::vector<std::uint8_t>;

struct QueueHandle {
  std::uint64_t native_id = 0;

  [[nodiscard]] bool has_consistent_values() const {
    return native_id != 0;
  }
};

struct QueuePushResult {
  bool enqueued = false;
  std::uint32_t queue_depth = 0;

  [[nodiscard]] bool has_consistent_values() const {
    return true;
  }
};

struct QueuePopResult {
  bool has_item = false;
  QueueItem item;
  std::uint32_t queue_depth = 0;

  [[nodiscard]] bool has_consistent_values() const {
    if (!has_item && !item.empty()) {
      return false;
    }

    return true;
  }
};

struct QueueCloseResult {
  bool already_closed = false;
  std::uint32_t dropped_items = 0;

  [[nodiscard]] bool has_consistent_values() const {
    return true;
  }
};

class IQueue {
 public:
  virtual ~IQueue() = default;

  virtual PlatformResult<QueueHandle> create_queue(const QueueOptions& options) = 0;
  virtual PlatformResult<QueuePushResult> push(const QueueHandle& handle,
                                               const QueueItem& item,
                                               std::int32_t timeout_ms) = 0;
  virtual PlatformResult<QueuePopResult> pop(const QueueHandle& handle,
                                             std::int32_t timeout_ms) = 0;
  virtual PlatformResult<QueueCloseResult> close(const QueueHandle& handle) = 0;
};

}  // namespace dasall::platform