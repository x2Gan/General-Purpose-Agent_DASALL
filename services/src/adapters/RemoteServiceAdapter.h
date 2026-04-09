#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "adapters/AdapterBridge.h"

namespace dasall::services::internal {

struct RemoteServiceAdapterOptions {
  bool remote_endpoint_available = true;
  bool timeout_on_invoke = false;
  std::string adapter_id = "remote_service.primary";
  std::function<AdapterInvocationResult(const AdapterInvocationRequest& request)> invoke_remote;
};

class RemoteServiceAdapter final : public IAdapterInvoker {
 public:
  explicit RemoteServiceAdapter(RemoteServiceAdapterOptions options);

  [[nodiscard]] std::string_view adapter_id() const override;
  [[nodiscard]] AdapterRouteKind route_kind() const override;
  [[nodiscard]] AdapterInvocationResult invoke(
      const AdapterInvocationRequest& request) const override;

 private:
  RemoteServiceAdapterOptions options_;
};

}  // namespace dasall::services::internal