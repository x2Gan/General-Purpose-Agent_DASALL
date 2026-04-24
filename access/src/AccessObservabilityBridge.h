#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "AccessErrors.h"
#include "AccessTypes.h"

namespace dasall::access {

struct AccessObservabilityEvent {
  std::string name;
  std::map<std::string, std::string> fields;
};

class AccessObservabilityBridge final {
 public:
  using EmitBackend = std::function<bool(const AccessObservabilityEvent& event)>;

  explicit AccessObservabilityBridge(EmitBackend backend = {});

  [[nodiscard]] bool emit_request_received(
      const InboundPacket& packet,
      std::string_view request_id,
      std::string_view session_id,
      std::string_view trace_id,
      std::optional<std::string_view> actor_ref = std::nullopt) const;

  [[nodiscard]] bool emit_auth_failed(
      const InboundPacket& packet,
      std::string_view request_id,
      std::string_view trace_id,
      std::string_view reason_code,
      std::optional<std::string_view> actor_ref = std::nullopt) const;

  [[nodiscard]] bool emit_policy_denied(
      const RuntimeDispatchRequest& request,
      std::string_view reason_code) const;

  [[nodiscard]] bool emit_dispatch_result(
      const RuntimeDispatchRequest& request,
      const RuntimeDispatchResult& result,
      std::int64_t latency_ms) const;

  [[nodiscard]] bool emit_publish_failed(
      const PublishEnvelope& envelope,
      AccessErrorCode error_code,
      std::string_view detail) const;

 private:
  [[nodiscard]] bool emit_event(AccessObservabilityEvent event) const;

  EmitBackend backend_;
};

}  // namespace dasall::access
