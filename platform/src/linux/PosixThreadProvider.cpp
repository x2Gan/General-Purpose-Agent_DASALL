#include "linux/PosixThreadProvider.h"

#include <optional>
#include <utility>

namespace dasall::platform::linux {

PlatformResult<ThreadHandle> PosixThreadProvider::create_thread(const ThreadOptions& options,
                                                                ThreadEntry entry) {
  if (!options.has_consistent_values() || !entry) {
    return PlatformResult<ThreadHandle>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "thread options or entry is invalid"));
  }

  if (options.stack_size_kb > kMaxSupportedStackKb) {
    return PlatformResult<ThreadHandle>::failure(
        make_error(PlatformErrorCode::ResourceExhausted,
                   PlatformErrorCategory::Resource,
                   "requested thread stack size exceeds skeleton limit"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const std::uint64_t id = next_id_++;
  threads_.emplace(id,
                   ThreadState{
                       .detach_policy = options.detach_policy,
                       .stop_requested = false,
                       .joined = false,
                   });

  return PlatformResult<ThreadHandle>::success(ThreadHandle{
      .native_id = id,
      .detach_policy = options.detach_policy,
  });
}

PlatformResult<ThreadJoinResult> PosixThreadProvider::join_thread(const ThreadHandle& handle,
                                                                  std::int32_t timeout_ms) {
  if (!handle.has_consistent_values() || timeout_ms < 0) {
    return PlatformResult<ThreadJoinResult>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "thread handle or timeout is invalid"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = threads_.find(handle.native_id);
  if (it == threads_.end()) {
    return PlatformResult<ThreadJoinResult>::failure(
        make_error(PlatformErrorCode::NotFound,
                   PlatformErrorCategory::Resource,
                   "thread handle does not exist"));
  }

  if (timeout_ms == 0 && !it->second.stop_requested) {
    return PlatformResult<ThreadJoinResult>::failure(
        make_error(PlatformErrorCode::Timeout,
                   PlatformErrorCategory::Resource,
                   "thread join timed out"));
  }

  it->second.joined = true;
  return PlatformResult<ThreadJoinResult>::success(ThreadJoinResult{.joined = true});
}

PlatformResult<bool> PosixThreadProvider::request_stop(const ThreadHandle& handle) {
  if (!handle.has_consistent_values()) {
    return PlatformResult<bool>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "thread handle is invalid"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = threads_.find(handle.native_id);
  if (it == threads_.end()) {
    return PlatformResult<bool>::failure(
        make_error(PlatformErrorCode::NotFound,
                   PlatformErrorCategory::Resource,
                   "thread handle does not exist"));
  }

  it->second.stop_requested = true;
  return PlatformResult<bool>::success(true);
}

PlatformError PosixThreadProvider::make_error(PlatformErrorCode code,
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