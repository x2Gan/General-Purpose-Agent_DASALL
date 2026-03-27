#pragma once

#include "linux/LinuxPlatformCapabilities.h"
#include "linux/PlatformInitConfig.h"

namespace dasall::platform::linux {

class HalAvailabilityBridge {
 public:
  [[nodiscard]] PlatformCapability probe_hal_availability(
      const PlatformInitConfig& config) const;
};

}  // namespace dasall::platform::linux