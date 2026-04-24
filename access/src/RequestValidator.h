#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "AccessErrors.h"
#include "AccessTypes.h"

namespace dasall::access {

// RequestValidationResult 统一承载请求校验结果。
struct RequestValidationResult {
  bool accepted = false;
  std::optional<AccessError> error;
};

// RequestValidator 负责 RuntimeBridge 前的输入校验与 fail-closed 拒绝。
class RequestValidator final {
 public:
  explicit RequestValidator(AccessPublishView publish_view = {},
                            std::vector<std::string> allowed_protocols = {});

  [[nodiscard]] RequestValidationResult validate_packet(
      const RuntimeDispatchRequest& request) const;

 private:
  [[nodiscard]] bool validate_payload_limits(
      const RuntimeDispatchRequest& request,
      RequestValidationResult* result) const;

  [[nodiscard]] bool validate_headers(
      const RuntimeDispatchRequest& request,
      RequestValidationResult* result) const;

  [[nodiscard]] bool is_allowed_protocol(std::string_view protocol_kind) const;

  AccessPublishView publish_view_;
  std::vector<std::string> allowed_protocols_;
};

}  // namespace dasall::access
