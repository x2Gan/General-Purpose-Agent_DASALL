#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "AccessErrors.h"
#include "IAccessRuntimeBridge.h"

namespace dasall::access {

// RuntimeBridge 将 access dispatch sidecar 收敛到 runtime-facing 调用与结果映射。
class RuntimeBridge final : public IAccessRuntimeBridge {
 public:
  using DispatchBackend =
      std::function<RuntimeDispatchResult(const RuntimeDispatchRequest& request)>;
  using CancelBackend =
      std::function<bool(std::string_view request_id, std::string_view actor_ref)>;

  RuntimeBridge(DispatchBackend dispatch_backend = {}, CancelBackend cancel_backend = {});

  [[nodiscard]] RuntimeDispatchResult dispatch(
      const RuntimeDispatchRequest& request) override;

  [[nodiscard]] bool cancel(
      std::string_view request_id,
      std::string_view actor_ref) override;

 private:
  [[nodiscard]] RuntimeDispatchResult map_runtime_result(
      const RuntimeDispatchRequest& request,
      const RuntimeDispatchResult& backend_result) const;

  [[nodiscard]] RuntimeDispatchResult map_runtime_reject(
      AccessErrorCode error_code,
      std::string reason,
      std::optional<std::string> detail = std::nullopt) const;

  [[nodiscard]] static std::optional<std::string> context_value(
      const RuntimeDispatchRequest& request,
      const std::string& key);

  DispatchBackend dispatch_backend_;
  CancelBackend cancel_backend_;
};

}  // namespace dasall::access
