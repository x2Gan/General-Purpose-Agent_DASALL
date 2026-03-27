#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "PlatformResult.h"

namespace dasall::platform {

enum class NetworkTransport {
  Tcp,
  Udp,
};

struct SocketEndpoint {
  std::string host;
  std::uint16_t port = 0;
  NetworkTransport transport = NetworkTransport::Tcp;

  [[nodiscard]] bool has_consistent_values() const {
    if (host.empty()) {
      return false;
    }

    if (port == 0U) {
      return false;
    }

    return true;
  }
};

struct ConnectOptions {
  std::int32_t connect_timeout_ms = 3000;
  bool reuse_address = false;

  [[nodiscard]] bool has_consistent_values() const {
    return connect_timeout_ms >= 0;
  }
};

struct ConnectionHandle {
  std::uint64_t native_fd = 0;

  [[nodiscard]] bool has_consistent_values() const {
    return native_fd != 0U;
  }
};

using NetworkBuffer = std::vector<std::uint8_t>;

struct NetworkSendResult {
  std::uint64_t bytes_sent = 0;

  [[nodiscard]] bool has_consistent_values() const {
    return true;
  }
};

struct NetworkReceiveResult {
  NetworkBuffer data;
  bool peer_closed = false;

  [[nodiscard]] bool has_consistent_values() const {
    if (peer_closed && !data.empty()) {
      return false;
    }

    return true;
  }
};

class INetwork {
 public:
  virtual ~INetwork() = default;

  virtual PlatformResult<ConnectionHandle> connect(const SocketEndpoint& endpoint,
                                                   const ConnectOptions& options) = 0;
  virtual PlatformResult<NetworkSendResult> send(const ConnectionHandle& handle,
                                                 const NetworkBuffer& buffer,
                                                 std::int32_t deadline_ms) = 0;
  virtual PlatformResult<NetworkReceiveResult> receive(const ConnectionHandle& handle,
                                                       std::int32_t deadline_ms) = 0;
  virtual PlatformResult<bool> shutdown(const ConnectionHandle& handle) = 0;
};

}  // namespace dasall::platform
