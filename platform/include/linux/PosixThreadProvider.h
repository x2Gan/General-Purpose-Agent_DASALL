#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "IThread.h"

namespace dasall::platform::linux {

class PosixThreadProvider final : public IThread {
 public:
  PosixThreadProvider() = default;

  PlatformResult<ThreadHandle> create_thread(const ThreadOptions& options,
                                             ThreadEntry entry) override;
  PlatformResult<ThreadJoinResult> join_thread(const ThreadHandle& handle,
                                               std::int32_t timeout_ms) override;
  PlatformResult<bool> request_stop(const ThreadHandle& handle) override;

 private:
  struct ThreadState {
    ThreadDetachPolicy detach_policy = ThreadDetachPolicy::Joinable;
    bool stop_requested = false;
    bool joined = false;
  };

  [[nodiscard]] PlatformError make_error(PlatformErrorCode code,
                                         PlatformErrorCategory category,
                                         std::string detail) const;

  static constexpr std::uint32_t kMaxSupportedStackKb = 4096;

  mutable std::mutex mutex_;
  std::uint64_t next_id_ = 1;
  std::unordered_map<std::uint64_t, ThreadState> threads_;
};

}  // namespace dasall::platform::linux