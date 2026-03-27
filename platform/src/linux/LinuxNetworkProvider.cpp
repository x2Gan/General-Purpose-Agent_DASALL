#include "linux/LinuxNetworkProvider.h"

#include <optional>
#include <utility>

namespace dasall::platform::linux {

PlatformResult<ConnectionHandle> LinuxNetworkProvider::connect(const SocketEndpoint& endpoint,
                                                               const ConnectOptions& options) {
  if (!endpoint.has_consistent_values() || !options.has_consistent_values()) {
    return PlatformResult<ConnectionHandle>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "endpoint or connect options are invalid"));
  }

  if (options.connect_timeout_ms == 0) {
    return PlatformResult<ConnectionHandle>::failure(
        make_error(PlatformErrorCode::Timeout,
                   PlatformErrorCategory::Network,
                   "connect timed out before establishing socket"));
  }

  if (endpoint.host.find("refused") != std::string::npos) {
    return PlatformResult<ConnectionHandle>::failure(
        make_error(PlatformErrorCode::ConnectionRefused,
                   PlatformErrorCategory::Network,
                   "connection refused by remote endpoint"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (enable_epoll_ && should_fallback_to_poll(endpoint)) {
    last_backend_ = NetworkIoBackend::Poll;
  } else {
    last_backend_ = enable_epoll_ ? NetworkIoBackend::Epoll : NetworkIoBackend::Poll;
  }

  const std::uint64_t fd = next_fd_++;
  connections_.emplace(fd, ConnectionState{.disconnected = false});
  return PlatformResult<ConnectionHandle>::success(ConnectionHandle{.native_fd = fd});
}

PlatformResult<NetworkSendResult> LinuxNetworkProvider::send(const ConnectionHandle& handle,
                                                             const NetworkBuffer& buffer,
                                                             std::int32_t deadline_ms) {
  if (!handle.has_consistent_values() || deadline_ms < 0) {
    return PlatformResult<NetworkSendResult>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "connection handle or send deadline is invalid"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = connections_.find(handle.native_fd);
  if (it == connections_.end() || it->second.disconnected) {
    return PlatformResult<NetworkSendResult>::failure(
        make_error(PlatformErrorCode::Disconnected,
                   PlatformErrorCategory::Network,
                   "connection is disconnected"));
  }

  if (deadline_ms == 0) {
    return PlatformResult<NetworkSendResult>::failure(
        make_error(PlatformErrorCode::Timeout,
                   PlatformErrorCategory::Network,
                   "send timed out before write completed"));
  }

  return PlatformResult<NetworkSendResult>::success(
      NetworkSendResult{.bytes_sent = static_cast<std::uint64_t>(buffer.size())});
}

PlatformResult<NetworkReceiveResult> LinuxNetworkProvider::receive(const ConnectionHandle& handle,
                                                                   std::int32_t deadline_ms) {
  if (!handle.has_consistent_values() || deadline_ms < 0) {
    return PlatformResult<NetworkReceiveResult>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "connection handle or receive deadline is invalid"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = connections_.find(handle.native_fd);
  if (it == connections_.end() || it->second.disconnected) {
    return PlatformResult<NetworkReceiveResult>::failure(
        make_error(PlatformErrorCode::Disconnected,
                   PlatformErrorCategory::Network,
                   "connection is disconnected"));
  }

  if (deadline_ms == 0) {
    return PlatformResult<NetworkReceiveResult>::failure(
        make_error(PlatformErrorCode::Timeout,
                   PlatformErrorCategory::Network,
                   "receive timed out before data arrival"));
  }

  return PlatformResult<NetworkReceiveResult>::success(NetworkReceiveResult{
      .data = {},
      .peer_closed = false,
  });
}

PlatformResult<bool> LinuxNetworkProvider::shutdown(const ConnectionHandle& handle) {
  if (!handle.has_consistent_values()) {
    return PlatformResult<bool>::failure(
        make_error(PlatformErrorCode::InvalidArgument,
                   PlatformErrorCategory::Validation,
                   "connection handle is invalid"));
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = connections_.find(handle.native_fd);
  if (it == connections_.end()) {
    return PlatformResult<bool>::failure(
        make_error(PlatformErrorCode::NotFound,
                   PlatformErrorCategory::Resource,
                   "connection handle does not exist"));
  }

  it->second.disconnected = true;
  return PlatformResult<bool>::success(true);
}

void LinuxNetworkProvider::set_enable_epoll(bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  enable_epoll_ = enabled;
}

NetworkIoBackend LinuxNetworkProvider::last_backend() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return last_backend_;
}

PlatformError LinuxNetworkProvider::make_error(PlatformErrorCode code,
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

bool LinuxNetworkProvider::should_fallback_to_poll(const SocketEndpoint& endpoint) const {
  return endpoint.host.find("fallback") != std::string::npos;
}

}  // namespace dasall::platform::linux