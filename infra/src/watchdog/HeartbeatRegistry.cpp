#include "watchdog/HeartbeatRegistry.h"

#include <string>
#include <string_view>

namespace dasall::infra::watchdog {
namespace {

constexpr std::string_view kHeartbeatRegistrySourceRef = "HeartbeatRegistry";

[[nodiscard]] HeartbeatRegistryRegisterResult make_register_failure(
    std::optional<WatchdogErrorCode> watchdog_code,
    contracts::ResultCode result_code,
    std::string message,
    std::string stage) {
  return HeartbeatRegistryRegisterResult::failure(
      watchdog_code,
      result_code,
      std::move(message),
      std::move(stage),
      std::string(kHeartbeatRegistrySourceRef));
}

[[nodiscard]] HeartbeatRegistryRemoveResult make_remove_failure(
    std::optional<WatchdogErrorCode> watchdog_code,
    contracts::ResultCode result_code,
    std::string message,
    std::string stage) {
  return HeartbeatRegistryRemoveResult::failure(
      watchdog_code,
      result_code,
      std::move(message),
      std::move(stage),
      std::string(kHeartbeatRegistrySourceRef));
}

[[nodiscard]] HeartbeatRegistryQueryResult make_query_failure(
    std::optional<WatchdogErrorCode> watchdog_code,
    contracts::ResultCode result_code,
    std::string message,
    std::string stage) {
  return HeartbeatRegistryQueryResult::failure(
      watchdog_code,
      result_code,
      std::move(message),
      std::move(stage),
      std::string(kHeartbeatRegistrySourceRef));
}

}  // namespace

HeartbeatRegistryRegisterResult HeartbeatRegistry::register_entity(
    const WatchedEntityDescriptor& descriptor) {
  if (!descriptor.has_required_fields()) {
    return make_register_failure(
        std::nullopt,
        contracts::ResultCode::ValidationFieldMissing,
        "heartbeat registry requires entity_id, entity_type, owner_module, criticality, timeout_ms, and grace_ms",
        "watchdog.registry.register");
  }

  if (entries_.size() >= max_entities_) {
    return make_register_failure(
        std::nullopt,
        contracts::ResultCode::ValidationFieldMissing,
        "heartbeat registry rejected the registration because max_entities was reached",
        "watchdog.registry.register");
  }

  const auto entity_id = descriptor.uniqueness_key();
  if (entries_.find(std::string(entity_id)) != entries_.end()) {
    const auto mapping = map_watchdog_error_code(WatchdogErrorCode::EntityDuplicate);
    return make_register_failure(
        WatchdogErrorCode::EntityDuplicate,
        mapping.result_code,
        std::string(watchdog_error_code_name(WatchdogErrorCode::EntityDuplicate)) +
            ": heartbeat registry rejects duplicate entity_id registrations",
        "watchdog.registry.register");
  }

  entries_.emplace(descriptor.entity_id, descriptor);
  return HeartbeatRegistryRegisterResult::success(descriptor, entries_.size());
}

HeartbeatRegistryRemoveResult HeartbeatRegistry::unregister_entity(
    std::string_view entity_id) {
  if (entity_id.empty()) {
    return make_remove_failure(
        std::nullopt,
        contracts::ResultCode::ValidationFieldMissing,
        "heartbeat registry requires a non-empty entity_id for unregister_entity",
        "watchdog.registry.unregister");
  }

  const auto entry = entries_.find(std::string(entity_id));
  if (entry == entries_.end()) {
    const auto mapping = map_watchdog_error_code(WatchdogErrorCode::EntityNotFound);
    return make_remove_failure(
        WatchdogErrorCode::EntityNotFound,
        mapping.result_code,
        std::string(watchdog_error_code_name(WatchdogErrorCode::EntityNotFound)) +
            ": heartbeat registry cannot unregister an unknown entity_id",
        "watchdog.registry.unregister");
  }

  WatchedEntityDescriptor descriptor = entry->second;
  entries_.erase(entry);
  return HeartbeatRegistryRemoveResult::success(std::move(descriptor),
                                                entries_.size());
}

HeartbeatRegistryQueryResult HeartbeatRegistry::query_entity(
    std::string_view entity_id) const {
  if (entity_id.empty()) {
    return make_query_failure(
        std::nullopt,
        contracts::ResultCode::ValidationFieldMissing,
        "heartbeat registry requires a non-empty entity_id for query_entity",
        "watchdog.registry.query");
  }

  const auto entry = entries_.find(std::string(entity_id));
  if (entry == entries_.end()) {
    const auto mapping = map_watchdog_error_code(WatchdogErrorCode::EntityNotFound);
    return make_query_failure(
        WatchdogErrorCode::EntityNotFound,
        mapping.result_code,
        std::string(watchdog_error_code_name(WatchdogErrorCode::EntityNotFound)) +
            ": heartbeat registry cannot query an unknown entity_id",
        "watchdog.registry.query");
  }

  return HeartbeatRegistryQueryResult::success(entry->second);
}

std::size_t HeartbeatRegistry::size() const {
  return entries_.size();
}

std::size_t HeartbeatRegistry::max_entities() const {
  return max_entities_;
}

}  // namespace dasall::infra::watchdog