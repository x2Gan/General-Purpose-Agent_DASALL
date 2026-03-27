#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "IIPC.h"

namespace dasall::platform::linux {

class UnixIpcProvider final : public IIPC {
 public:
  UnixIpcProvider() = default;

  PlatformResult<IpcListenerHandle> listen(const IpcEndpoint& endpoint,
                                           const ListenOptions& options) override;
  PlatformResult<IpcChannelHandle> accept(const IpcListenerHandle& listener,
                                          std::int32_t deadline_ms) override;
  PlatformResult<IpcChannelHandle> connect(const IpcEndpoint& endpoint,
                                           std::int32_t deadline_ms) override;
  PlatformResult<IpcSendResult> send(const IpcChannelHandle& handle,
                                     const IpcPayload& payload) override;
  PlatformResult<IpcReceiveResult> receive(const IpcChannelHandle& handle,
                                           std::int32_t deadline_ms) override;
  PlatformResult<bool> close(const IpcChannelHandle& handle) override;

 private:
  struct ListenerState {
    ListenOptions options;
  };

  struct ChannelState {
    bool closed = false;
    bool peer_closed = false;
    std::uint32_t max_payload_bytes = 1048576U;
  };

  [[nodiscard]] PlatformError make_error(PlatformErrorCode code,
                                         PlatformErrorCategory category,
                                         std::string detail) const;

  mutable std::mutex mutex_;
  std::uint64_t next_listener_fd_ = 200;
  std::uint64_t next_channel_fd_ = 500;
  std::unordered_map<std::uint64_t, ListenerState> listeners_;
  std::unordered_map<std::uint64_t, ChannelState> channels_;
};

}  // namespace dasall::platform::linux