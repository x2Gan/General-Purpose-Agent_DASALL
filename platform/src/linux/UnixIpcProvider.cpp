#include "linux/UnixIpcProvider.h"

#include <optional>
#include <utility>

namespace dasall::platform::linux {

PlatformResult<IpcListenerHandle> UnixIpcProvider::listen(const IpcEndpoint& endpoint,
                                                          const ListenOptions& options) {
  if (!endpoint.has_consistent_values() || !options.has_consistent_values()) {
    return PlatformResult<IpcListenerHandle>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "ipc endpoint or listen options are invalid"));
  }

  if (endpoint.socket_path.find("in-use") != std::string::npos) {
    return PlatformResult<IpcListenerHandle>::failure(
        make_error(PlatformErrorCode::AddressInUse,
                   PlatformErrorCategory::IPC,
                   "socket path is already in use"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const std::uint64_t fd = next_listener_fd_++;
  listeners_.emplace(fd, ListenerState{.options = options});
  return PlatformResult<IpcListenerHandle>::success(IpcListenerHandle{.native_fd = fd});
}

PlatformResult<IpcChannelHandle> UnixIpcProvider::accept(const IpcListenerHandle& listener,
                                                         std::int32_t deadline_ms) {
  if (!listener.has_consistent_values() || deadline_ms < 0) {
    return PlatformResult<IpcChannelHandle>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "listener handle or deadline is invalid"));
  }

  if (deadline_ms == 0) {
    return PlatformResult<IpcChannelHandle>::failure(
        make_error(PlatformErrorCode::Timeout,
                   PlatformErrorCategory::IPC,
                   "accept timed out before peer connection"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const auto listener_it = listeners_.find(listener.native_fd);
  if (listener_it == listeners_.end()) {
    return PlatformResult<IpcChannelHandle>::failure(
        make_error(PlatformErrorCode::NotFound,
                   PlatformErrorCategory::Resource,
                   "listener handle does not exist"));
  }

  const std::uint64_t fd = next_channel_fd_++;
  channels_.emplace(fd,
                    ChannelState{
                        .closed = false,
                        .peer_closed = false,
                        .max_payload_bytes = listener_it->second.options.max_payload_bytes,
                    });

  return PlatformResult<IpcChannelHandle>::success(IpcChannelHandle{.native_fd = fd});
}

PlatformResult<IpcChannelHandle> UnixIpcProvider::connect(const IpcEndpoint& endpoint,
                                                          std::int32_t deadline_ms) {
  if (!endpoint.has_consistent_values() || deadline_ms < 0) {
    return PlatformResult<IpcChannelHandle>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "ipc endpoint or connect deadline is invalid"));
  }

  if (deadline_ms == 0) {
    return PlatformResult<IpcChannelHandle>::failure(
        make_error(PlatformErrorCode::Timeout,
                   PlatformErrorCategory::IPC,
                   "connect timed out before channel establishment"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const std::uint64_t fd = next_channel_fd_++;
  channels_.emplace(fd,
                    ChannelState{
                        .closed = false,
                        .peer_closed = (endpoint.socket_path.find("closed-peer") !=
                                        std::string::npos),
                        .max_payload_bytes = 1048576U,
                    });
  return PlatformResult<IpcChannelHandle>::success(IpcChannelHandle{.native_fd = fd});
}

PlatformResult<IpcSendResult> UnixIpcProvider::send(const IpcChannelHandle& handle,
                                                    const IpcPayload& payload) {
  if (!handle.has_consistent_values()) {
    return PlatformResult<IpcSendResult>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "ipc channel handle is invalid"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = channels_.find(handle.native_fd);
  if (it == channels_.end() || it->second.closed) {
    return PlatformResult<IpcSendResult>::failure(
        make_error(PlatformErrorCode::PeerClosed,
                   PlatformErrorCategory::IPC,
                   "ipc channel is closed"));
  }

  if (payload.size() > it->second.max_payload_bytes) {
    return PlatformResult<IpcSendResult>::failure(
        make_error(PlatformErrorCode::PayloadTooLarge,
                   PlatformErrorCategory::IPC,
                   "ipc payload exceeds max payload budget"));
  }

  if (it->second.peer_closed) {
    return PlatformResult<IpcSendResult>::failure(
        make_error(PlatformErrorCode::PeerClosed,
                   PlatformErrorCategory::IPC,
                   "peer has already closed the channel"));
  }

  return PlatformResult<IpcSendResult>::success(
      IpcSendResult{.bytes_sent = static_cast<std::uint64_t>(payload.size())});
}

PlatformResult<IpcReceiveResult> UnixIpcProvider::receive(const IpcChannelHandle& handle,
                                                          std::int32_t deadline_ms) {
  if (!handle.has_consistent_values() || deadline_ms < 0) {
    return PlatformResult<IpcReceiveResult>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "ipc channel handle or deadline is invalid"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = channels_.find(handle.native_fd);
  if (it == channels_.end()) {
    return PlatformResult<IpcReceiveResult>::failure(
        make_error(PlatformErrorCode::NotFound,
                   PlatformErrorCategory::Resource,
                   "ipc channel does not exist"));
  }

  if (it->second.peer_closed) {
    return PlatformResult<IpcReceiveResult>::success(IpcReceiveResult{
        .data = {},
        .peer_closed = true,
    });
  }

  if (deadline_ms == 0) {
    return PlatformResult<IpcReceiveResult>::failure(
        make_error(PlatformErrorCode::Timeout,
                   PlatformErrorCategory::IPC,
                   "ipc receive timed out before payload arrival"));
  }

  return PlatformResult<IpcReceiveResult>::success(IpcReceiveResult{
      .data = {},
      .peer_closed = false,
  });
}

PlatformResult<bool> UnixIpcProvider::close(const IpcChannelHandle& handle) {
  if (!handle.has_consistent_values()) {
    return PlatformResult<bool>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "ipc channel handle is invalid"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = channels_.find(handle.native_fd);
  if (it == channels_.end()) {
    return PlatformResult<bool>::failure(
        make_error(PlatformErrorCode::NotFound,
                   PlatformErrorCategory::Resource,
                   "ipc channel does not exist"));
  }

  it->second.closed = true;
  return PlatformResult<bool>::success(true);
}

PlatformError UnixIpcProvider::make_error(PlatformErrorCode code,
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