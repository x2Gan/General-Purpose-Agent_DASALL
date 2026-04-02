#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace dasall::infra::watchdog {

enum class WatchdogEntityCriticality {
  Unspecified = 0,
  NonCritical = 1,
  Critical = 2,
};

inline constexpr std::string_view watchdog_entity_criticality_name(
    WatchdogEntityCriticality criticality) {
  switch (criticality) {
    case WatchdogEntityCriticality::Unspecified:
      return "unspecified";
    case WatchdogEntityCriticality::NonCritical:
      return "non_critical";
    case WatchdogEntityCriticality::Critical:
      return "critical";
  }

  return "unspecified";
}

struct WatchedEntityDescriptor {
  std::string entity_id;
  std::string entity_type;
  std::string owner_module;
  WatchdogEntityCriticality criticality = WatchdogEntityCriticality::Unspecified;
  std::uint32_t timeout_ms = 0;
  std::uint32_t grace_ms = 0;

  [[nodiscard]] bool has_required_fields() const {
    return !entity_id.empty() && !entity_type.empty() && !owner_module.empty() &&
           criticality != WatchdogEntityCriticality::Unspecified && timeout_ms > 0 &&
           grace_ms < timeout_ms;
  }

  [[nodiscard]] std::string uniqueness_key() const {
    return entity_id;
  }

  [[nodiscard]] bool reuses_entity_id_of(
      const WatchedEntityDescriptor& other) const {
    return !entity_id.empty() && entity_id == other.entity_id;
  }
};

}  // namespace dasall::infra::watchdog