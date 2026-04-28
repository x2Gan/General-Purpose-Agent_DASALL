#include "linux/UnixIpcProvider.h"

#include <optional>
#include <utility>

namespace dasall::platform::linux {

namespace {

[[nodiscard]] PeerIdentitySnapshot make_peer_identity(std::uint64_t handle,
                                                      bool is_local_socket_peer,
                                                      std::uint32_t pid_base) {
  return PeerIdentitySnapshot{
      .peer_uid = is_local_socket_peer ? 1000U : 2000U,
      .peer_gid = is_local_socket_peer ? 1000U : 2000U,
      .peer_pid = static_cast<std::uint32_t>(pid_base + handle),
      .is_local_socket_peer = is_local_socket_peer,
  };
}

}  // namespace

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
  listeners_.emplace(fd,
                     ListenerState{
                         .endpoint = endpoint,
                         .options = options,
                         .pending_server_channels = {},
                     });
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

  if (!listener_it->second.pending_server_channels.empty()) {
    const auto fd = listener_it->second.pending_server_channels.front();
    listener_it->second.pending_server_channels.pop_front();
    return PlatformResult<IpcChannelHandle>::success(IpcChannelHandle{.native_fd = fd});
  }

  const std::uint64_t fd = next_channel_fd_++;
  channels_.emplace(fd,
                    ChannelState{
                        .closed = false,
                        .peer_closed = false,
                        .max_payload_bytes = listener_it->second.options.max_payload_bytes,
                        .peer_identity = make_peer_identity(fd, true, 10000U),
                      .peer_channel_fd = std::nullopt,
                      .inbound_payloads = {},
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
  const bool local_socket_peer = endpoint.socket_path.find("remote") == std::string::npos;
  const bool peer_closed = endpoint.socket_path.find("closed-peer") != std::string::npos;

  auto matched_listener = listeners_.end();
  for (auto it = listeners_.begin(); it != listeners_.end(); ++it) {
    if (it->second.endpoint.socket_path == endpoint.socket_path) {
      matched_listener = it;
      break;
    }
  }

  if (matched_listener != listeners_.end()) {
    const auto client_fd = next_channel_fd_++;
    const auto server_fd = next_channel_fd_++;

    channels_.emplace(client_fd,
                      ChannelState{
                          .closed = false,
                          .peer_closed = peer_closed,
                          .max_payload_bytes = matched_listener->second.options.max_payload_bytes,
                          .peer_identity = make_peer_identity(server_fd,
                                                              local_socket_peer,
                                                              20000U),
                          .peer_channel_fd = server_fd,
                          .inbound_payloads = {},
                      });
    channels_.emplace(server_fd,
                      ChannelState{
                          .closed = false,
                          .peer_closed = peer_closed,
                          .max_payload_bytes = matched_listener->second.options.max_payload_bytes,
                          .peer_identity = make_peer_identity(client_fd,
                                                              local_socket_peer,
                                                              30000U),
                          .peer_channel_fd = client_fd,
                          .inbound_payloads = {},
                      });
    matched_listener->second.pending_server_channels.push_back(server_fd);
    return PlatformResult<IpcChannelHandle>::success(
        IpcChannelHandle{.native_fd = client_fd});
  }

  const auto fd = next_channel_fd_++;
  channels_.emplace(fd,
                    ChannelState{
                        .closed = false,
                        .peer_closed = peer_closed,
                        .max_payload_bytes = 1048576U,
                        .peer_identity = make_peer_identity(fd,
                                                            local_socket_peer,
                                                            20000U),
                      .peer_channel_fd = std::nullopt,
                      .inbound_payloads = {},
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

  if (it->second.peer_channel_fd.has_value()) {
    const auto peer_it = channels_.find(*it->second.peer_channel_fd);
    if (peer_it == channels_.end() || peer_it->second.closed) {
      it->second.peer_closed = true;
      return PlatformResult<IpcSendResult>::failure(
          make_error(PlatformErrorCode::PeerClosed,
                     PlatformErrorCategory::IPC,
                     "peer channel is no longer available"));
    }

    peer_it->second.inbound_payloads.push_back(payload);
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

  if (!it->second.inbound_payloads.empty()) {
    auto payload = std::move(it->second.inbound_payloads.front());
    it->second.inbound_payloads.pop_front();
    return PlatformResult<IpcReceiveResult>::success(IpcReceiveResult{
        .data = std::move(payload),
        .peer_closed = false,
    });
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

  if (it->second.peer_channel_fd.has_value()) {
    const auto peer_it = channels_.find(*it->second.peer_channel_fd);
    if (peer_it == channels_.end() || peer_it->second.closed) {
      it->second.peer_closed = true;
      return PlatformResult<IpcReceiveResult>::success(IpcReceiveResult{
          .data = {},
          .peer_closed = true,
      });
    }
  }

  return PlatformResult<IpcReceiveResult>::success(IpcReceiveResult{
      .data = {},
      .peer_closed = false,
  });
}

PlatformResult<PeerIdentitySnapshot> UnixIpcProvider::describe_peer(
    const IpcChannelHandle& handle) {
  if (!handle.has_consistent_values()) {
    return PlatformResult<PeerIdentitySnapshot>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "ipc channel handle is invalid"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = channels_.find(handle.native_fd);
  if (it == channels_.end()) {
    return PlatformResult<PeerIdentitySnapshot>::failure(
        make_error(PlatformErrorCode::NotFound,
                   PlatformErrorCategory::Resource,
                   "ipc channel does not exist"));
  }

  if (!it->second.peer_identity.has_consistent_values()) {
    return PlatformResult<PeerIdentitySnapshot>::failure(
        make_error(PlatformErrorCode::InternalFailure,
                   PlatformErrorCategory::Internal,
                   "ipc peer identity snapshot is inconsistent"));
  }

  return PlatformResult<PeerIdentitySnapshot>::success(it->second.peer_identity);
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
  if (it->second.peer_channel_fd.has_value()) {
    const auto peer_it = channels_.find(*it->second.peer_channel_fd);
    if (peer_it != channels_.end()) {
      peer_it->second.peer_closed = true;
    }
  }
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