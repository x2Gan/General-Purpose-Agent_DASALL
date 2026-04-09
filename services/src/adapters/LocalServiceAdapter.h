#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "adapters/AdapterBridge.h"

namespace dasall::services::internal {

struct LocalServiceAdapterOptions {
  bool service_endpoint_available = true;
  std::string adapter_id = "local_service.primary";
  std::function<AdapterInvocationResult(const AdapterInvocationRequest& request)> invoke_service;
};

class LocalServiceAdapter final : public IAdapterInvoker {
 public:
  explicit LocalServiceAdapter(LocalServiceAdapterOptions options);

  [[nodiscard]] std::string_view adapter_id() const override;
  [[nodiscard]] AdapterRouteKind route_kind() const override;
  [[nodiscard]] AdapterInvocationResult invoke(
      const AdapterInvocationRequest& request) const override;

 private:
  LocalServiceAdapterOptions options_;
};

}  // namespace dasall::services::internal