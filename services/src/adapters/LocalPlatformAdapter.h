#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "adapters/AdapterBridge.h"

namespace dasall::services::internal {

struct LocalPlatformAdapterOptions {
  bool platform_hal_enabled = false;
  std::string adapter_id = "local_platform.primary";
  std::function<AdapterInvocationResult(const AdapterInvocationRequest& request)> invoke_platform;
};

class LocalPlatformAdapter final : public IAdapterInvoker {
 public:
  explicit LocalPlatformAdapter(LocalPlatformAdapterOptions options);

  [[nodiscard]] std::string_view adapter_id() const override;
  [[nodiscard]] AdapterRouteKind route_kind() const override;
  [[nodiscard]] AdapterInvocationResult invoke(
      const AdapterInvocationRequest& request) const override;

 private:
  LocalPlatformAdapterOptions options_;
};

}  // namespace dasall::services::internal