#include "CliIpcClient.h"

#include <cstdint>
#include <utility>

namespace dasall::apps::cli {

CliIpcClient::CliIpcClient(std::shared_ptr<dasall::platform::IIPC> ipc,
                           dasall::platform::IpcEndpoint endpoint,
                           const std::int32_t connect_deadline_ms)
    : ipc_(std::move(ipc)),
      endpoint_(std::move(endpoint)),
      connect_deadline_ms_(connect_deadline_ms) {}

bool CliIpcClient::ping_daemon() const {
  return send_payload("{\"op\":\"ping\"}");
}

bool CliIpcClient::send_payload(const std::string_view payload) const {
  if (!ipc_ || !endpoint_.has_consistent_values() || connect_deadline_ms_ < 0) {
    return false;
  }

  const auto channel = ipc_->connect(endpoint_, connect_deadline_ms_);
  if (!channel.ok() || !channel.value.has_value()) {
    return false;
  }

  dasall::platform::IpcPayload ipc_payload;
  ipc_payload.reserve(payload.size());
  for (const char c : payload) {
    ipc_payload.push_back(static_cast<std::uint8_t>(c));
  }

  const auto sent = ipc_->send(*channel.value, ipc_payload);
  return sent.ok();
}

}  // namespace dasall::apps::cli
