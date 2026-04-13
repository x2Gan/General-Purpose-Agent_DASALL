#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "HealthStatus.h"
#include "ILLMAdapter.h"

#include "ModelRouter.h"

namespace dasall::llm::route {

struct AdapterRegistryConfig {
  std::uint32_t blocked_failure_threshold = 3U;

  [[nodiscard]] bool has_consistent_values() const;
};

struct AdapterRegistration {
  std::string provider_id;
  std::string model_id;
  std::string adapter_id;
  std::string deployment_type;
  std::vector<std::string> capability_tags;
  bool supports_streaming = false;
  std::shared_ptr<ILLMAdapter> adapter;

  [[nodiscard]] std::string route_key() const;
  [[nodiscard]] bool has_consistent_values() const;
};

struct AdapterRouteState {
  std::string provider_id;
  std::string model_id;
  std::string adapter_id;
  std::string deployment_type;
  std::vector<std::string> capability_tags;
  bool supports_streaming = false;
  std::shared_ptr<ILLMAdapter> adapter;
  HealthStatus last_health;
  bool blocked = false;
  std::uint32_t consecutive_failures = 0U;

  [[nodiscard]] std::string route_key() const;
  [[nodiscard]] bool has_consistent_values() const;
};

struct AdapterRegistrySnapshot {
  std::vector<AdapterRouteState> routes;

  [[nodiscard]] bool has_consistent_values() const;
  [[nodiscard]] const AdapterRouteState* find_route(std::string_view route_key) const;
  [[nodiscard]] std::optional<AdapterRouteState> resolve_route(std::string_view route_key) const;
  [[nodiscard]] std::vector<std::string> route_keys() const;
  [[nodiscard]] ModelRouterHealthSnapshot to_model_router_health_snapshot() const;
};

class AdapterRegistry {
 public:
  bool init(const AdapterRegistryConfig& config = {});
  bool register_adapter(const AdapterRegistration& registration);
  bool unregister_adapter(std::string_view route_key);

  [[nodiscard]] std::shared_ptr<const AdapterRegistrySnapshot> snapshot() const;
  [[nodiscard]] std::optional<AdapterRouteState> resolve_route(std::string_view route_key) const;
  [[nodiscard]] ModelRouterHealthSnapshot health_snapshot() const;
  [[nodiscard]] std::string last_error_message() const;

  bool probe_health(std::string_view route_key);
  bool probe_all_health();
  bool record_call_success(std::string_view route_key, std::string message = {});
  bool record_call_failure(std::string_view route_key, std::string message = {});

 private:
  AdapterRegistryConfig config_;
  std::shared_ptr<const AdapterRegistrySnapshot> snapshot_;
  std::string last_error_message_;
  bool initialized_ = false;
  mutable std::mutex write_mutex_;
};

}  // namespace dasall::llm::route
