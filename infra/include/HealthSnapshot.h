#pragma once

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace dasall::infra {

struct HealthSnapshot {
  using ComponentList = std::vector<std::string>;

  bool liveness = false;
  bool readiness = false;
  bool degraded = false;
  ComponentList failed_components;

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

  [[nodiscard]] bool is_ready() const {
    return has_consistent_state() && liveness && readiness && !degraded;
  }

  [[nodiscard]] bool is_degraded_state() const {
    return has_consistent_state() && liveness && degraded;
  }

  [[nodiscard]] bool is_failed_state() const {
    return has_consistent_state() && !liveness;
  }
};

}  // namespace dasall::infra