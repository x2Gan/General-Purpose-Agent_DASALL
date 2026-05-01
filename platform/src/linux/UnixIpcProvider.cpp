#include "linux/UnixIpcProvider.h"

#include <chrono>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

namespace dasall::platform::linux {

namespace {

struct NativeAddress final {
  sockaddr_un value{};
  socklen_t length = 0;
};

struct PeerCredentials final {
  pid_t pid = 0;
  uid_t uid = 0;
  gid_t gid = 0;
};

[[nodiscard]] std::optional<NativeAddress> make_native_address(
    const IpcEndpoint& endpoint) {
  NativeAddress address;
  address.value.sun_family = AF_UNIX;

  if (endpoint.use_abstract_namespace) {
    if (endpoint.socket_path.size() + 1U > sizeof(address.value.sun_path)) {
      return std::nullopt;
    }

    address.value.sun_path[0] = '\0';
    std::memcpy(address.value.sun_path + 1,
                endpoint.socket_path.data(),
                endpoint.socket_path.size());
    address.length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) +
                                            1U + endpoint.socket_path.size());
    return address;
  }

  if (endpoint.socket_path.size() >= sizeof(address.value.sun_path)) {
    return std::nullopt;
  }

  std::memcpy(address.value.sun_path,
              endpoint.socket_path.c_str(),
              endpoint.socket_path.size() + 1U);
  address.length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) +
                                          endpoint.socket_path.size() + 1U);
  return address;
}

[[nodiscard]] int create_unix_socket() {
  int native_fd = ::socket(AF_UNIX, SOCK_SEQPACKET, 0);
  if (native_fd < 0) {
    return native_fd;
  }

  const int flags = ::fcntl(native_fd, F_GETFD, 0);
  if (flags >= 0) {
    (void)::fcntl(native_fd, F_SETFD, flags | FD_CLOEXEC);
  }

  return native_fd;
}

[[nodiscard]] bool is_retryable_connect_errno(const int error_value) {
  return error_value == ENOENT || error_value == ECONNREFUSED ||
         error_value == EAGAIN || error_value == EINTR;
}

[[nodiscard]] PlatformErrorCode map_errno_code(const int error_value,
                                               const bool for_connect) {
  if (error_value == EACCES || error_value == EPERM) {
    return PlatformErrorCode::PermissionDenied;
  }

  if (error_value == EADDRINUSE) {
    return PlatformErrorCode::AddressInUse;
  }

  if (error_value == ENOENT) {
    return for_connect ? PlatformErrorCode::Timeout : PlatformErrorCode::NotFound;
  }

  if (error_value == ECONNREFUSED) {
    return for_connect ? PlatformErrorCode::Timeout
                       : PlatformErrorCode::ConnectionRefused;
  }

  if (error_value == ENOBUFS || error_value == ENOMEM) {
    return PlatformErrorCode::ResourceExhausted;
  }

  if (error_value == EPIPE || error_value == ECONNRESET ||
      error_value == ENOTCONN) {
    return PlatformErrorCode::PeerClosed;
  }

  if (error_value == ETIMEDOUT || error_value == EAGAIN ||
      error_value == EWOULDBLOCK) {
    return PlatformErrorCode::Timeout;
  }

  return PlatformErrorCode::InternalFailure;
}

[[nodiscard]] PlatformErrorCategory map_errno_category(const int error_value) {
  if (error_value == EACCES || error_value == EPERM) {
    return PlatformErrorCategory::Validation;
  }

  if (error_value == EADDRINUSE || error_value == ENOENT ||
      error_value == ENOBUFS || error_value == ENOMEM) {
    return PlatformErrorCategory::Resource;
  }

  if (error_value == ECONNREFUSED || error_value == EPIPE ||
      error_value == ECONNRESET || error_value == ENOTCONN ||
      error_value == ETIMEDOUT || error_value == EAGAIN ||
      error_value == EWOULDBLOCK) {
    return PlatformErrorCategory::IPC;
  }

  return PlatformErrorCategory::IO;
}

[[nodiscard]] bool wait_for_events(const int native_fd,
                                   const short events,
                                   const std::int32_t timeout_ms) {
  pollfd descriptor{
      .fd = native_fd,
      .events = events,
      .revents = 0,
  };

  int poll_result = 0;
  do {
    poll_result = ::poll(&descriptor, 1, timeout_ms);
  } while (poll_result < 0 && errno == EINTR);

  if (poll_result <= 0) {
    return false;
  }

  return (descriptor.revents & (events | POLLHUP | POLLERR)) != 0;
}

[[nodiscard]] std::optional<PeerIdentitySnapshot> query_peer_identity(
    const int native_fd,
    const bool is_local_socket_peer) {
  if (!is_local_socket_peer) {
    return PeerIdentitySnapshot{
        .peer_uid = 0U,
        .peer_gid = 0U,
        .peer_pid = 0U,
        .is_local_socket_peer = false,
    };
  }

  PeerCredentials credentials;
  socklen_t credentials_size = sizeof(credentials);
  if (::getsockopt(native_fd,
                   SOL_SOCKET,
                   SO_PEERCRED,
                   &credentials,
                   &credentials_size) != 0) {
    return std::nullopt;
  }

  return PeerIdentitySnapshot{
      .peer_uid = static_cast<std::uint32_t>(credentials.uid),
      .peer_gid = static_cast<std::uint32_t>(credentials.gid),
      .peer_pid = static_cast<std::uint32_t>(credentials.pid),
      .is_local_socket_peer = true,
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

  const auto native_address = make_native_address(endpoint);
  if (!native_address.has_value()) {
    return PlatformResult<IpcListenerHandle>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "socket path exceeds unix domain socket length budget"));
  }

  const int native_fd = create_unix_socket();
  if (native_fd < 0) {
    const int error_value = errno;
    return PlatformResult<IpcListenerHandle>::failure(
        make_error(map_errno_code(error_value, false),
                   map_errno_category(error_value),
                   std::string("socket() failed while creating listener: ") +
                       std::strerror(error_value),
                   "socket",
                   error_value));
  }

  if (::bind(native_fd,
             reinterpret_cast<const sockaddr*>(&native_address->value),
             native_address->length) != 0) {
    const int error_value = errno;
    (void)::close(native_fd);
    return PlatformResult<IpcListenerHandle>::failure(
        make_error(map_errno_code(error_value, false),
                   map_errno_category(error_value),
                   std::string("bind() failed for socket path '") + endpoint.socket_path +
                       "': " + std::strerror(error_value),
                   "bind",
                   error_value));
  }

  if (::listen(native_fd, static_cast<int>(options.backlog)) != 0) {
    const int error_value = errno;
    if (!endpoint.use_abstract_namespace) {
      (void)::unlink(endpoint.socket_path.c_str());
    }
    (void)::close(native_fd);
    return PlatformResult<IpcListenerHandle>::failure(
        make_error(map_errno_code(error_value, false),
                   map_errno_category(error_value),
                   std::string("listen() failed for socket path '") + endpoint.socket_path +
                       "': " + std::strerror(error_value),
                   "listen",
                   error_value));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  listeners_.emplace(static_cast<std::uint64_t>(native_fd),
                     ListenerState{
                         .endpoint = endpoint,
                         .options = options,
                     });
  return PlatformResult<IpcListenerHandle>::success(
      IpcListenerHandle{.native_fd = static_cast<std::uint64_t>(native_fd)});
}

PlatformResult<IpcChannelHandle> UnixIpcProvider::accept(const IpcListenerHandle& listener,
                                                         std::int32_t deadline_ms) {
  if (!listener.has_consistent_values() || deadline_ms < 0) {
    return PlatformResult<IpcChannelHandle>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "listener handle or deadline is invalid"));
  }

  ListenOptions options;
  std::string socket_path;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto listener_it = listeners_.find(listener.native_fd);
    if (listener_it == listeners_.end()) {
      return PlatformResult<IpcChannelHandle>::failure(
          make_error(PlatformErrorCode::NotFound,
                     PlatformErrorCategory::Resource,
                     "listener handle does not exist"));
    }

    options = listener_it->second.options;
    socket_path = listener_it->second.endpoint.socket_path;
  }

  if (!wait_for_events(static_cast<int>(listener.native_fd), POLLIN, deadline_ms)) {
    return PlatformResult<IpcChannelHandle>::failure(
        make_error(PlatformErrorCode::Timeout,
                   PlatformErrorCategory::IPC,
                   "accept timed out before peer connection"));
  }

  const int accepted_fd = ::accept(static_cast<int>(listener.native_fd), nullptr, nullptr);
  if (accepted_fd < 0) {
    const int error_value = errno;
    return PlatformResult<IpcChannelHandle>::failure(
        make_error(map_errno_code(error_value, false),
                   map_errno_category(error_value),
                   std::string("accept() failed: ") + std::strerror(error_value),
                   "accept",
                   error_value));
  }

  const int flags = ::fcntl(accepted_fd, F_GETFD, 0);
  if (flags >= 0) {
    (void)::fcntl(accepted_fd, F_SETFD, flags | FD_CLOEXEC);
  }

  std::lock_guard<std::mutex> lock(mutex_);
  channels_.emplace(static_cast<std::uint64_t>(accepted_fd),
                    ChannelState{
                        .closed = false,
                        .max_payload_bytes = options.max_payload_bytes,
                        .socket_path = std::move(socket_path),
                    });
  return PlatformResult<IpcChannelHandle>::success(
      IpcChannelHandle{.native_fd = static_cast<std::uint64_t>(accepted_fd)});
}

PlatformResult<IpcChannelHandle> UnixIpcProvider::connect(const IpcEndpoint& endpoint,
                                                          std::int32_t deadline_ms) {
  if (!endpoint.has_consistent_values() || deadline_ms < 0) {
    return PlatformResult<IpcChannelHandle>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "ipc endpoint or connect deadline is invalid"));
  }

  const auto native_address = make_native_address(endpoint);
  if (!native_address.has_value()) {
    return PlatformResult<IpcChannelHandle>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "socket path exceeds unix domain socket length budget"));
  }

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(deadline_ms);
  while (true) {
    const int native_fd = create_unix_socket();
    if (native_fd < 0) {
      const int error_value = errno;
      return PlatformResult<IpcChannelHandle>::failure(
          make_error(map_errno_code(error_value, true),
                     map_errno_category(error_value),
                     std::string("socket() failed while creating client channel: ") +
                         std::strerror(error_value),
                     "socket",
                     error_value));
    }

    if (::connect(native_fd,
                  reinterpret_cast<const sockaddr*>(&native_address->value),
                  native_address->length) == 0) {
      std::uint32_t max_payload_bytes = 1048576U;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [listener_fd, listener_state] : listeners_) {
          (void)listener_fd;
          if (listener_state.endpoint.socket_path == endpoint.socket_path) {
            max_payload_bytes = listener_state.options.max_payload_bytes;
            break;
          }
        }

        channels_.emplace(static_cast<std::uint64_t>(native_fd),
                          ChannelState{
                              .closed = false,
                              .max_payload_bytes = max_payload_bytes,
                              .socket_path = endpoint.socket_path,
                          });
      }

      return PlatformResult<IpcChannelHandle>::success(
          IpcChannelHandle{.native_fd = static_cast<std::uint64_t>(native_fd)});
    }

    const int error_value = errno;
    (void)::close(native_fd);
    if (std::chrono::steady_clock::now() >= deadline ||
        !is_retryable_connect_errno(error_value)) {
      return PlatformResult<IpcChannelHandle>::failure(
          make_error(map_errno_code(error_value, true),
                     map_errno_category(error_value),
                     std::string("connect() failed for socket path '") +
                         endpoint.socket_path + "': " + std::strerror(error_value),
                     "connect",
                     error_value));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

PlatformResult<IpcSendResult> UnixIpcProvider::send(const IpcChannelHandle& handle,
                                                    const IpcPayload& payload) {
  if (!handle.has_consistent_values()) {
    return PlatformResult<IpcSendResult>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "ipc channel handle is invalid"));
  }

  int native_fd = -1;
  std::uint32_t max_payload_bytes = 0U;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = channels_.find(handle.native_fd);
    if (it == channels_.end() || it->second.closed) {
      return PlatformResult<IpcSendResult>::failure(
          make_error(PlatformErrorCode::PeerClosed,
                     PlatformErrorCategory::IPC,
                     "ipc channel is closed"));
    }

    native_fd = static_cast<int>(handle.native_fd);
    max_payload_bytes = it->second.max_payload_bytes;
  }

  if (payload.size() > max_payload_bytes) {
    return PlatformResult<IpcSendResult>::failure(
        make_error(PlatformErrorCode::PayloadTooLarge,
                   PlatformErrorCategory::IPC,
                   "ipc payload exceeds max payload budget"));
  }

  std::size_t bytes_sent = 0U;
  while (bytes_sent < payload.size()) {
    const auto send_result = ::send(native_fd,
                                    payload.data() + bytes_sent,
                                    payload.size() - bytes_sent,
                                    MSG_NOSIGNAL);
    if (send_result < 0) {
      const int error_value = errno;
      return PlatformResult<IpcSendResult>::failure(
          make_error(map_errno_code(error_value, false),
                     map_errno_category(error_value),
                     std::string("send() failed: ") + std::strerror(error_value),
                     "send",
                     error_value));
    }

    bytes_sent += static_cast<std::size_t>(send_result);
  }

  return PlatformResult<IpcSendResult>::success(
      IpcSendResult{.bytes_sent = static_cast<std::uint64_t>(bytes_sent)});
}

PlatformResult<IpcReceiveResult> UnixIpcProvider::receive(const IpcChannelHandle& handle,
                                                          std::int32_t deadline_ms) {
  if (!handle.has_consistent_values() || deadline_ms < 0) {
    return PlatformResult<IpcReceiveResult>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "ipc channel handle or deadline is invalid"));
  }

  int native_fd = -1;
  std::uint32_t max_payload_bytes = 0U;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = channels_.find(handle.native_fd);
    if (it == channels_.end() || it->second.closed) {
      return PlatformResult<IpcReceiveResult>::failure(
          make_error(PlatformErrorCode::NotFound,
                     PlatformErrorCategory::Resource,
                     "ipc channel does not exist"));
    }

    native_fd = static_cast<int>(handle.native_fd);
    max_payload_bytes = it->second.max_payload_bytes;
  }

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(deadline_ms);
  while (true) {
    if (wait_for_events(native_fd, POLLIN, 0)) {
      IpcPayload payload(max_payload_bytes);
      const auto recv_result = ::recv(native_fd, payload.data(), payload.size(), 0);
      if (recv_result == 0) {
        return PlatformResult<IpcReceiveResult>::success(IpcReceiveResult{
            .data = {},
            .peer_closed = true,
        });
      }

      if (recv_result < 0) {
        const int error_value = errno;
        if (error_value == EAGAIN || error_value == EWOULDBLOCK || error_value == EINTR) {
          // Continue polling until deadline.
        } else if (error_value == EPIPE || error_value == ECONNRESET ||
                   error_value == ENOTCONN) {
          return PlatformResult<IpcReceiveResult>::success(IpcReceiveResult{
              .data = {},
              .peer_closed = true,
          });
        } else {
          return PlatformResult<IpcReceiveResult>::failure(
              make_error(map_errno_code(error_value, false),
                         map_errno_category(error_value),
                         std::string("recv() failed: ") + std::strerror(error_value),
                         "recv",
                         error_value));
        }
      } else {
        payload.resize(static_cast<std::size_t>(recv_result));
        return PlatformResult<IpcReceiveResult>::success(IpcReceiveResult{
            .data = std::move(payload),
            .peer_closed = false,
        });
      }
    }

    if (std::chrono::steady_clock::now() >= deadline) {
      return PlatformResult<IpcReceiveResult>::failure(
          make_error(PlatformErrorCode::Timeout,
                     PlatformErrorCategory::IPC,
                     "ipc receive timed out before payload arrival"));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

PlatformResult<PeerIdentitySnapshot> UnixIpcProvider::describe_peer(
    const IpcChannelHandle& handle) {
  if (!handle.has_consistent_values()) {
    return PlatformResult<PeerIdentitySnapshot>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "ipc channel handle is invalid"));
  }

  std::string socket_path;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = channels_.find(handle.native_fd);
    if (it == channels_.end()) {
      return PlatformResult<PeerIdentitySnapshot>::failure(
          make_error(PlatformErrorCode::NotFound,
                     PlatformErrorCategory::Resource,
                     "ipc channel does not exist"));
    }

    socket_path = it->second.socket_path;
  }

  const bool is_local_socket_peer = socket_path.find("remote") == std::string::npos;
  const auto peer_identity = query_peer_identity(static_cast<int>(handle.native_fd),
                                                 is_local_socket_peer);
  if (!peer_identity.has_value()) {
    const int error_value = errno;
    return PlatformResult<PeerIdentitySnapshot>::failure(
        make_error(PlatformErrorCode::InternalFailure,
                   PlatformErrorCategory::IPC,
                   "getsockopt(SO_PEERCRED) failed for active channel",
                   "getsockopt",
                   error_value));
  }

  return PlatformResult<PeerIdentitySnapshot>::success(*peer_identity);
}

PlatformResult<bool> UnixIpcProvider::close(const IpcChannelHandle& handle) {
  if (!handle.has_consistent_values()) {
    return PlatformResult<bool>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "ipc channel handle is invalid"));
  }

  std::optional<ListenerState> listener_state;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto listener_it = listeners_.find(handle.native_fd);
    if (listener_it != listeners_.end()) {
      listener_state = listener_it->second;
      listeners_.erase(listener_it);
    } else {
      auto channel_it = channels_.find(handle.native_fd);
      if (channel_it == channels_.end()) {
        return PlatformResult<bool>::failure(
            make_error(PlatformErrorCode::NotFound,
                       PlatformErrorCategory::Resource,
                       "ipc channel does not exist"));
      }

      channel_it->second.closed = true;
      channels_.erase(channel_it);
    }
  }

  const int native_fd = static_cast<int>(handle.native_fd);
  (void)::shutdown(native_fd, SHUT_RDWR);
  if (::close(native_fd) != 0) {
    const int error_value = errno;
    return PlatformResult<bool>::failure(
        make_error(map_errno_code(error_value, false),
                   map_errno_category(error_value),
                   std::string("close() failed: ") + std::strerror(error_value),
                   "close",
                   error_value));
  }

  if (listener_state.has_value() && !listener_state->endpoint.use_abstract_namespace) {
    (void)::unlink(listener_state->endpoint.socket_path.c_str());
  }

  return PlatformResult<bool>::success(true);
}

PlatformError UnixIpcProvider::make_error(PlatformErrorCode code,
                                          PlatformErrorCategory category,
                                          std::string detail,
                                          std::string syscall_name,
                                          std::optional<int> errno_value) const {
  return PlatformError{
      .code = code,
      .category = category,
      .retryable_hint = (code == PlatformErrorCode::Timeout),
      .syscall_name = std::move(syscall_name),
      .errno_value = errno_value,
      .detail = std::move(detail),
  };
}

}  // namespace dasall::platform::linux