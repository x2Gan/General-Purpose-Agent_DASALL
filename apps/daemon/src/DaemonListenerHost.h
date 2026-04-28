#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include "IIPC.h"
#include "PlatformResult.h"

namespace dasall::apps::daemon {

class DaemonListenerHost {
 public:
  using ConnectionHandler =
      std::function<bool(const dasall::platform::IpcChannelHandle& channel)>;

  explicit DaemonListenerHost(std::shared_ptr<dasall::platform::IIPC> ipc);

  [[nodiscard]] dasall::platform::PlatformResult<bool> bind(
      const dasall::platform::IpcEndpoint& endpoint);

  void set_connection_handler(ConnectionHandler handler);

  [[nodiscard]] dasall::platform::PlatformResult<bool> accept_loop(
      const std::atomic<bool>& stop_requested,
      std::int32_t accept_deadline_ms);

  [[nodiscard]] dasall::platform::PlatformResult<bool> close();

  [[nodiscard]] bool is_bound() const;

 private:
  std::shared_ptr<dasall::platform::IIPC> ipc_;
  std::optional<dasall::platform::IpcListenerHandle> listener_;
  ConnectionHandler connection_handler_;
  bool closed_ = false;

  static constexpr std::uint32_t kListenBacklog = 8U;
  static constexpr std::uint32_t kMaxPayloadBytes = 1048576U;
};

}  // namespace dasall::apps::daemon