#pragma once

#include <functional>
#include <optional>
#include <string>

#include "AccessErrors.h"
#include "AccessTypes.h"
#include "ProtocolErrorMapper.h"

namespace dasall::access {

struct PublishAttemptResult {
  bool published = false;
  PublishEnvelope envelope;
  std::optional<AccessError> error;
};

// ResultPublisher 负责把 AgentResult 与 access sidecar 合成为统一发布请求。
class ResultPublisher final {
 public:
  using EmitBackend = std::function<bool(const PublishEnvelope& envelope)>;

  explicit ResultPublisher(EmitBackend emit_backend = {});

  [[nodiscard]] PublishEnvelope build_envelope(
      const RuntimeDispatchRequest& request,
      const dasall::contracts::AgentResult& agent_result) const;

  [[nodiscard]] AccessProtocolErrorMapping map_protocol_status(
      const dasall::contracts::AgentResult& agent_result) const;

  [[nodiscard]] bool emit_publish(const PublishEnvelope& envelope) const;

  [[nodiscard]] PublishAttemptResult publish(
      const RuntimeDispatchRequest& request,
      const dasall::contracts::AgentResult& agent_result) const;

 private:
  [[nodiscard]] static std::optional<std::string> context_value(
      const RuntimeDispatchRequest& request,
      const std::string& key);

  EmitBackend emit_backend_;
};

}  // namespace dasall::access
