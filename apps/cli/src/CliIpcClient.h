#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "IIPC.h"

namespace dasall::apps::cli {

class CliIpcClient {
 public:
  CliIpcClient(std::shared_ptr<dasall::platform::IIPC> ipc,
               dasall::platform::IpcEndpoint endpoint,
               std::int32_t connect_deadline_ms = 1000);

  [[nodiscard]] bool ping_daemon() const;
  [[nodiscard]] bool send_payload(std::string_view payload) const;

 private:
  std::shared_ptr<dasall::platform::IIPC> ipc_;
  dasall::platform::IpcEndpoint endpoint_;
  std::int32_t connect_deadline_ms_ = 1000;
};

}  // namespace dasall::apps::cli
