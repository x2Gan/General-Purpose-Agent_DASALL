#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dasall::infra {

enum class HealthState {
  Unknown = 0,
  Healthy = 1,
  Degraded = 2,
  Unhealthy = 3,
};

struct HealthSnapshot {
  using ComponentList = std::vector<std::string>;

  bool liveness = false;
  bool readiness = false;
  bool degraded = false;
  ComponentList failed_components;
  std::uint64_t version = 0;
  std::int64_t timestamp = 0;

  [[nodiscard]] static bool is_reserved_runtime_state_name(
      std::string_view component_name) {
    static constexpr std::array<std::string_view, 4> kReservedRuntimeStateNames = {
        "final_runtime_state",
        "fsm_state",
        "retry_count",
        "checkpoint_ref",
    };

    return std::find(kReservedRuntimeStateNames.begin(),
                     kReservedRuntimeStateNames.end(),
                     component_name) != kReservedRuntimeStateNames.end();
  }

  [[nodiscard]] bool failed_components_are_valid() const {
    for (std::size_t index = 0; index < failed_components.size(); ++index) {
      if (failed_components[index].empty() ||
          is_reserved_runtime_state_name(failed_components[index])) {
        return false;
      }

      if (std::find(failed_components.begin() + static_cast<std::ptrdiff_t>(index + 1),
                    failed_components.end(),
                    failed_components[index]) != failed_components.end()) {
        return false;
      }
    }

    return true;
  }

  [[nodiscard]] bool has_consistent_state() const {
    if (!failed_components_are_valid()) {
      return false;
    }

    if (!liveness && (readiness || degraded)) {
      return false;
    }

    if (readiness && !liveness) {
      return false;
    }

    if (degraded && !liveness) {
      return false;
    }

    return true;
  }

  [[nodiscard]] bool has_version_metadata() const {
    return version > 0 && timestamp > 0;
  }

  [[nodiscard]] bool is_newer_than(const HealthSnapshot& previous) const {
    return has_version_metadata() && previous.has_version_metadata() &&
           version > previous.version && timestamp >= previous.timestamp;
  }

  [[nodiscard]] HealthState state() const {
    if (!has_consistent_state()) {
      return HealthState::Unknown;
    }

    if (!liveness) {
      return HealthState::Unhealthy;
    }

    if (!readiness || degraded) {
      return HealthState::Degraded;
    }

    return HealthState::Healthy;
  }

  [[nodiscard]] bool is_ready() const {
    return state() == HealthState::Healthy;
  }

  [[nodiscard]] bool is_degraded_state() const {
    return state() == HealthState::Degraded;
  }

  [[nodiscard]] bool is_failed_state() const {
    return state() == HealthState::Unhealthy;
  }
};

struct HealthTransition {
  HealthState from_state = HealthState::Unknown;
  HealthState to_state = HealthState::Unknown;
  std::string reason;
  std::string trigger_probe;
  std::int64_t timestamp = 0;

  [[nodiscard]] bool is_state_change() const {
    return from_state != HealthState::Unknown && to_state != HealthState::Unknown &&
           from_state != to_state;
  }

  [[nodiscard]] bool has_required_fields() const {
    return is_state_change() && !reason.empty() && !trigger_probe.empty() &&
           timestamp > 0;
  }
};

}  // namespace dasall::infra