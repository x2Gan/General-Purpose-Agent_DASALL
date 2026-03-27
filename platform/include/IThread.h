#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "PlatformResult.h"

namespace dasall::platform {

enum class ThreadDetachPolicy {
  Joinable,
  Detached,
};

struct ThreadOptions {
  std::string name = "platform-worker";
  std::uint32_t stack_size_kb = 512;
  ThreadDetachPolicy detach_policy = ThreadDetachPolicy::Joinable;
  std::optional<std::uint16_t> affinity_hint;

  [[nodiscard]] bool has_consistent_values() const {
    if (name.empty()) {
      return false;
    }

    if (stack_size_kb == 0U) {
      return false;
    }

    return true;
  }
};

struct ThreadHandle {
  std::uint64_t native_id = 0;
  ThreadDetachPolicy detach_policy = ThreadDetachPolicy::Joinable;

  [[nodiscard]] bool has_consistent_values() const {
    return native_id != 0;
  }
};

struct ThreadJoinResult {
  bool joined = false;
};

using ThreadEntry = std::function<void()>;

class IThread {
 public:
  virtual ~IThread() = default;

  virtual PlatformResult<ThreadHandle> create_thread(const ThreadOptions& options,
                                                     ThreadEntry entry) = 0;
  virtual PlatformResult<ThreadJoinResult> join_thread(const ThreadHandle& handle,
                                                        std::int32_t timeout_ms) = 0;
  virtual PlatformResult<bool> request_stop(const ThreadHandle& handle) = 0;
};

}  // namespace dasall::platform