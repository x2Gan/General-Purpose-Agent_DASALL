#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "PlatformResult.h"

namespace dasall::platform {

struct IpcEndpoint {
  std::string socket_path;
  bool use_abstract_namespace = false;

  [[nodiscard]] bool has_consistent_values() const {
    return !socket_path.empty();
  }
};

struct ListenOptions {
  std::uint32_t backlog = 5;
  std::uint32_t max_payload_bytes = 1048576;

  [[nodiscard]] bool has_consistent_values() const {
    if (backlog == 0U) {
      return false;
    }

    if (max_payload_bytes == 0U) {
      return false;
    }

    return true;
  }
};

struct IpcListenerHandle {
  std::uint64_t native_fd = 0;

  [[nodiscard]] bool has_consistent_values() const {
    return native_fd != 0U;
  }
};

struct IpcChannelHandle {
  std::uint64_t native_fd = 0;

  [[nodiscard]] bool has_consistent_values() const {
    return native_fd != 0U;
  }
};

using IpcPayload = std::vector<std::uint8_t>;

struct IpcSendResult {
  std::uint64_t bytes_sent = 0;

  [[nodiscard]] bool has_consistent_values() const {
    return true;
  }
};

struct IpcReceiveResult {
  IpcPayload data;
  bool peer_closed = false;

  [[nodiscard]] bool has_consistent_values() const {
    if (peer_closed && !data.empty()) {
      return false;
    }

    return true;
  }
};

class IIPC {
 public:
  virtual ~IIPC() = default;

  virtual PlatformResult<IpcListenerHandle> listen(const IpcEndpoint& endpoint,
                                                   const ListenOptions& options) = 0;
  virtual PlatformResult<IpcChannelHandle> accept(const IpcListenerHandle& listener,
                                                  std::int32_t deadline_ms) = 0;
  virtual PlatformResult<IpcChannelHandle> connect(const IpcEndpoint& endpoint,
                                                   std::int32_t deadline_ms) = 0;
  virtual PlatformResult<IpcSendResult> send(const IpcChannelHandle& handle,
                                             const IpcPayload& payload) = 0;
  virtual PlatformResult<IpcReceiveResult> receive(const IpcChannelHandle& handle,
                                                   std::int32_t deadline_ms) = 0;
  virtual PlatformResult<bool> close(const IpcChannelHandle& handle) = 0;
};

}  // namespace dasall::platform
