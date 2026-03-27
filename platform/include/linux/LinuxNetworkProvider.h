#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "INetwork.h"

namespace dasall::platform::linux {

enum class NetworkIoBackend {
  Epoll,
  Poll,
};

class LinuxNetworkProvider final : public INetwork {
 public:
  LinuxNetworkProvider() = default;

  PlatformResult<ConnectionHandle> connect(const SocketEndpoint& endpoint,
                                           const ConnectOptions& options) override;
  PlatformResult<NetworkSendResult> send(const ConnectionHandle& handle,
                                         const NetworkBuffer& buffer,
                                         std::int32_t deadline_ms) override;
  PlatformResult<NetworkReceiveResult> receive(const ConnectionHandle& handle,
                                               std::int32_t deadline_ms) override;
  PlatformResult<bool> shutdown(const ConnectionHandle& handle) override;

  void set_enable_epoll(bool enabled);
  [[nodiscard]] NetworkIoBackend last_backend() const;

 private:
  struct ConnectionState {
    bool disconnected = false;
  };

  [[nodiscard]] PlatformError make_error(PlatformErrorCode code,
                                         PlatformErrorCategory category,
                                         std::string detail) const;
  [[nodiscard]] bool should_fallback_to_poll(const SocketEndpoint& endpoint) const;

  mutable std::mutex mutex_;
  bool enable_epoll_ = true;
  NetworkIoBackend last_backend_ = NetworkIoBackend::Epoll;
  std::uint64_t next_fd_ = 100;
  std::unordered_map<std::uint64_t, ConnectionState> connections_;
};

}  // namespace dasall::platform::linux