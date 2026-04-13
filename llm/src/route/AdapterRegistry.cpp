#include "AdapterRegistry.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using AdapterRegistration = dasall::llm::route::AdapterRegistration;
using AdapterRegistryConfig = dasall::llm::route::AdapterRegistryConfig;
using AdapterRegistrySnapshot = dasall::llm::route::AdapterRegistrySnapshot;
using AdapterRouteState = dasall::llm::route::AdapterRouteState;
using HealthStatus = dasall::llm::HealthStatus;
using ModelRouterHealthSnapshot = dasall::llm::route::ModelRouterHealthSnapshot;
using ModelRouterHealthState = dasall::llm::route::ModelRouterHealthState;

std::string make_route_key(std::string_view provider_id, std::string_view model_id) {
  return std::string(provider_id) + "/" + std::string(model_id);
}

void sort_routes(std::vector<AdapterRouteState>& routes) {
  std::sort(routes.begin(), routes.end(), [](const AdapterRouteState& left, const AdapterRouteState& right) {
    return left.route_key() < right.route_key();
  });
}

HealthStatus normalize_health_status(HealthStatus status, std::string_view fallback_message) {
  if (status.message.empty()) {
    status.message = std::string(fallback_message);
  }

  return status;
}

bool route_is_blocked_by_failures(std::uint32_t consecutive_failures,
                                  const AdapterRegistryConfig& config) {
  return consecutive_failures >= config.blocked_failure_threshold;
}

AdapterRouteState make_route_state(const AdapterRegistration& registration,
                                   const AdapterRouteState* existing_state) {
  AdapterRouteState state;
  state.provider_id = registration.provider_id;
  state.model_id = registration.model_id;
  state.adapter_id = registration.adapter_id;
  state.deployment_type = registration.deployment_type;
  state.capability_tags = registration.capability_tags;
  state.supports_streaming = registration.supports_streaming;
  state.adapter = registration.adapter;

  if (existing_state != nullptr) {
    state.last_health = existing_state->last_health;
    state.blocked = existing_state->blocked;
    state.consecutive_failures = existing_state->consecutive_failures;
    return state;
  }

  state.last_health = HealthStatus{
      .ready = false,
      .degraded = false,
      .message = "adapter registered but not probed",
  };
  return state;
}

AdapterRouteState* find_route_mutable(std::vector<AdapterRouteState>& routes,
                                      std::string_view route_key) {
  const auto it = std::find_if(routes.begin(), routes.end(), [&](const AdapterRouteState& route) {
    return route.route_key() == route_key;
  });
  return it == routes.end() ? nullptr : &(*it);
}

const AdapterRouteState* find_route_const(const std::vector<AdapterRouteState>& routes,
                                          std::string_view route_key) {
  const auto it = std::find_if(routes.begin(), routes.end(), [&](const AdapterRouteState& route) {
    return route.route_key() == route_key;
  });
  return it == routes.end() ? nullptr : &(*it);
}

}  // namespace

namespace dasall::llm::route {

bool AdapterRegistryConfig::has_consistent_values() const {
  return blocked_failure_threshold > 0U;
}

std::string AdapterRegistration::route_key() const {
  return make_route_key(provider_id, model_id);
}

bool AdapterRegistration::has_consistent_values() const {
  return !provider_id.empty() && !model_id.empty() && !adapter_id.empty() &&
         !deployment_type.empty() && adapter != nullptr;
}

std::string AdapterRouteState::route_key() const {
  return make_route_key(provider_id, model_id);
}

bool AdapterRouteState::has_consistent_values() const {
  return !provider_id.empty() && !model_id.empty() && !adapter_id.empty() &&
         !deployment_type.empty() && adapter != nullptr;
}

bool AdapterRegistrySnapshot::has_consistent_values() const {
  std::unordered_set<std::string> route_keys;
  route_keys.reserve(routes.size());

  for (const auto& route : routes) {
    if (!route.has_consistent_values()) {
      return false;
    }

    if (!route_keys.insert(route.route_key()).second) {
      return false;
    }
  }

  return true;
}

const AdapterRouteState* AdapterRegistrySnapshot::find_route(std::string_view route_key) const {
  return find_route_const(routes, route_key);
}

std::optional<AdapterRouteState> AdapterRegistrySnapshot::resolve_route(
    std::string_view route_key) const {
  const auto* route = find_route(route_key);
  if (route == nullptr) {
    return std::nullopt;
  }

  return *route;
}

std::vector<std::string> AdapterRegistrySnapshot::route_keys() const {
  std::vector<std::string> keys;
  keys.reserve(routes.size());

  for (const auto& route : routes) {
    keys.push_back(route.route_key());
  }

  return keys;
}

ModelRouterHealthSnapshot AdapterRegistrySnapshot::to_model_router_health_snapshot() const {
  ModelRouterHealthSnapshot snapshot;
  snapshot.route_states.reserve(routes.size());

  for (const auto& route : routes) {
    snapshot.route_states.push_back(ModelRouterHealthState{
        .provider_id = route.provider_id,
        .model_id = route.model_id,
        .blocked = route.blocked,
        .consecutive_failures = route.consecutive_failures,
    });
  }

  return snapshot;
}

bool AdapterRegistry::init(const AdapterRegistryConfig& config) {
  if (!config.has_consistent_values()) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    last_error_message_ = "adapter registry received inconsistent config";
    return false;
  }

  config_ = config;
  initialized_ = true;

  auto empty_snapshot = std::make_shared<const AdapterRegistrySnapshot>();
  std::atomic_store_explicit(&snapshot_, empty_snapshot, std::memory_order_release);

  std::lock_guard<std::mutex> lock(write_mutex_);
  last_error_message_.clear();
  return true;
}

bool AdapterRegistry::register_adapter(const AdapterRegistration& registration) {
  if (!initialized_) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    last_error_message_ = "adapter registry has not been initialized";
    return false;
  }

  if (!registration.has_consistent_values()) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    last_error_message_ = "adapter registry received inconsistent registration";
    return false;
  }

  std::lock_guard<std::mutex> lock(write_mutex_);
  auto current_snapshot = std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
  AdapterRegistrySnapshot next_snapshot = current_snapshot == nullptr ? AdapterRegistrySnapshot{}
                                                                     : *current_snapshot;

  AdapterRouteState* existing_state = find_route_mutable(next_snapshot.routes, registration.route_key());
  const AdapterRouteState preserved_state = existing_state == nullptr ? AdapterRouteState{} : *existing_state;
  AdapterRouteState replacement = make_route_state(registration,
                                                   existing_state == nullptr ? nullptr : &preserved_state);

  if (existing_state == nullptr) {
    next_snapshot.routes.push_back(std::move(replacement));
  } else {
    *existing_state = std::move(replacement);
  }

  sort_routes(next_snapshot.routes);
  auto immutable_snapshot = std::make_shared<const AdapterRegistrySnapshot>(std::move(next_snapshot));
  std::atomic_store_explicit(&snapshot_, immutable_snapshot, std::memory_order_release);
  last_error_message_.clear();
  return true;
}

bool AdapterRegistry::unregister_adapter(std::string_view route_key) {
  if (!initialized_) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    last_error_message_ = "adapter registry has not been initialized";
    return false;
  }

  std::lock_guard<std::mutex> lock(write_mutex_);
  auto current_snapshot = std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
  AdapterRegistrySnapshot next_snapshot = current_snapshot == nullptr ? AdapterRegistrySnapshot{}
                                                                     : *current_snapshot;

  const auto original_size = next_snapshot.routes.size();
  next_snapshot.routes.erase(
      std::remove_if(next_snapshot.routes.begin(), next_snapshot.routes.end(),
                     [&](const AdapterRouteState& route) { return route.route_key() == route_key; }),
      next_snapshot.routes.end());

  if (next_snapshot.routes.size() == original_size) {
    last_error_message_ = "adapter registry route not found";
    return false;
  }

  auto immutable_snapshot = std::make_shared<const AdapterRegistrySnapshot>(std::move(next_snapshot));
  std::atomic_store_explicit(&snapshot_, immutable_snapshot, std::memory_order_release);
  last_error_message_.clear();
  return true;
}

std::shared_ptr<const AdapterRegistrySnapshot> AdapterRegistry::snapshot() const {
  return std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
}

std::optional<AdapterRouteState> AdapterRegistry::resolve_route(std::string_view route_key) const {
  const auto current_snapshot = snapshot();
  if (current_snapshot == nullptr) {
    return std::nullopt;
  }

  return current_snapshot->resolve_route(route_key);
}

ModelRouterHealthSnapshot AdapterRegistry::health_snapshot() const {
  const auto current_snapshot = snapshot();
  if (current_snapshot == nullptr) {
    return {};
  }

  return current_snapshot->to_model_router_health_snapshot();
}

std::string AdapterRegistry::last_error_message() const {
  std::lock_guard<std::mutex> lock(write_mutex_);
  return last_error_message_;
}

bool AdapterRegistry::probe_health(std::string_view route_key) {
  if (!initialized_) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    last_error_message_ = "adapter registry has not been initialized";
    return false;
  }

  const auto current_snapshot = snapshot();
  if (current_snapshot == nullptr) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    last_error_message_ = "adapter registry snapshot is not available";
    return false;
  }

  const auto* route = current_snapshot->find_route(route_key);
  if (route == nullptr) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    last_error_message_ = "adapter registry route not found";
    return false;
  }

  const HealthStatus raw_status = route->adapter->health_check();
  const HealthStatus status = normalize_health_status(
      raw_status,
      raw_status.ready ? (raw_status.degraded ? "adapter health degraded" : "adapter health ready")
                       : "adapter health not ready");

  std::lock_guard<std::mutex> lock(write_mutex_);
  auto latest_snapshot = std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
  AdapterRegistrySnapshot next_snapshot = latest_snapshot == nullptr ? AdapterRegistrySnapshot{}
                                                                    : *latest_snapshot;

  AdapterRouteState* mutable_route = find_route_mutable(next_snapshot.routes, route_key);
  if (mutable_route == nullptr) {
    last_error_message_ = "adapter registry route disappeared before health update";
    return false;
  }

  mutable_route->last_health = status;
  if (status.ready && !status.degraded) {
    mutable_route->blocked = false;
    mutable_route->consecutive_failures = 0U;
  } else {
    ++mutable_route->consecutive_failures;
    mutable_route->blocked = !status.ready ||
                             route_is_blocked_by_failures(mutable_route->consecutive_failures, config_);
  }

  auto immutable_snapshot = std::make_shared<const AdapterRegistrySnapshot>(std::move(next_snapshot));
  std::atomic_store_explicit(&snapshot_, immutable_snapshot, std::memory_order_release);
  last_error_message_.clear();
  return true;
}

bool AdapterRegistry::probe_all_health() {
  if (!initialized_) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    last_error_message_ = "adapter registry has not been initialized";
    return false;
  }

  const auto current_snapshot = snapshot();
  if (current_snapshot == nullptr) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    last_error_message_ = "adapter registry snapshot is not available";
    return false;
  }

  bool all_succeeded = true;
  for (const auto& route_key : current_snapshot->route_keys()) {
    all_succeeded = probe_health(route_key) && all_succeeded;
  }

  return all_succeeded;
}

bool AdapterRegistry::record_call_success(std::string_view route_key, std::string message) {
  if (!initialized_) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    last_error_message_ = "adapter registry has not been initialized";
    return false;
  }

  std::lock_guard<std::mutex> lock(write_mutex_);
  auto current_snapshot = std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
  AdapterRegistrySnapshot next_snapshot = current_snapshot == nullptr ? AdapterRegistrySnapshot{}
                                                                     : *current_snapshot;
  AdapterRouteState* route = find_route_mutable(next_snapshot.routes, route_key);
  if (route == nullptr) {
    last_error_message_ = "adapter registry route not found";
    return false;
  }

  route->blocked = false;
  route->consecutive_failures = 0U;
  route->last_health = normalize_health_status(
      HealthStatus{.ready = true, .degraded = false, .message = std::move(message)},
      "adapter route healthy after successful call");

  auto immutable_snapshot = std::make_shared<const AdapterRegistrySnapshot>(std::move(next_snapshot));
  std::atomic_store_explicit(&snapshot_, immutable_snapshot, std::memory_order_release);
  last_error_message_.clear();
  return true;
}

bool AdapterRegistry::record_call_failure(std::string_view route_key, std::string message) {
  if (!initialized_) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    last_error_message_ = "adapter registry has not been initialized";
    return false;
  }

  std::lock_guard<std::mutex> lock(write_mutex_);
  auto current_snapshot = std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
  AdapterRegistrySnapshot next_snapshot = current_snapshot == nullptr ? AdapterRegistrySnapshot{}
                                                                     : *current_snapshot;
  AdapterRouteState* route = find_route_mutable(next_snapshot.routes, route_key);
  if (route == nullptr) {
    last_error_message_ = "adapter registry route not found";
    return false;
  }

  ++route->consecutive_failures;
  route->blocked = route_is_blocked_by_failures(route->consecutive_failures, config_);
  route->last_health = normalize_health_status(
      HealthStatus{.ready = !route->blocked, .degraded = true, .message = std::move(message)},
      route->blocked ? "adapter route blocked after consecutive call failures"
                     : "adapter route degraded after call failure");

  auto immutable_snapshot = std::make_shared<const AdapterRegistrySnapshot>(std::move(next_snapshot));
  std::atomic_store_explicit(&snapshot_, immutable_snapshot, std::memory_order_release);
  last_error_message_.clear();
  return true;
}

}  // namespace dasall::llm::route
