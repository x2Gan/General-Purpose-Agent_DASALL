#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "ServiceTypes.h"
#include "adapters/AdapterBridge.h"
#include "adapters/AdapterRouter.h"
#include "logging/ILogger.h"

namespace dasall::services::internal {

struct ServiceLoggingBridgeOptions {
  bool enabled = true;
  std::function<std::int64_t()> now_ms;
};

class ServiceLoggingBridge {
 public:
  explicit ServiceLoggingBridge(
      std::shared_ptr<infra::logging::ILogger> logger = nullptr,
      ServiceLoggingBridgeOptions options = {});

  void set_logger(std::shared_ptr<infra::logging::ILogger> logger);

  [[nodiscard]] infra::logging::LogWriteResult write_execution_route(
      const ServiceCallContext& context,
      const CapabilityTargetRef& target,
      const std::string& action,
      const AdapterSelection& selection,
      const AdapterReceipt& receipt) const;

  [[nodiscard]] infra::logging::LogWriteResult write_data_query_route(
      const ServiceCallContext& context,
      const std::string& dataset,
      const std::string& projection,
      const AdapterSelection& selection,
      const AdapterReceipt& receipt) const;

  [[nodiscard]] infra::logging::LogWriteResult write_data_catalog_route(
      const ServiceCallContext& context,
      const std::string& target_class,
      const AdapterSelection& selection,
      const AdapterReceipt& receipt) const;

  [[nodiscard]] bool is_enabled() const {
    return options_.enabled;
  }

 private:
  [[nodiscard]] infra::logging::LogWriteResult write_route_event(
      const std::string& event_name,
      const ServiceCallContext& context,
      const std::string& capability_id,
      const std::string& target_id,
      const std::string& request_kind,
      const std::string& operation_name,
      const AdapterSelection& selection,
      const AdapterReceipt& receipt) const;

  std::shared_ptr<infra::logging::ILogger> logger_;
  ServiceLoggingBridgeOptions options_{};
};

}  // namespace dasall::services::internal