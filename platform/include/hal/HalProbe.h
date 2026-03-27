#pragma once

#include <string>

namespace dasall::platform::arm::hal {

struct HalProbeResult {
  bool available = false;
  std::string reason = "HalStubOnly";

  [[nodiscard]] bool has_consistent_values() const {
    if (available) {
      return reason.empty();
    }

    return !reason.empty();
  }
};

[[nodiscard]] HalProbeResult probe_hal_availability();

}  // namespace dasall::platform::arm::hal