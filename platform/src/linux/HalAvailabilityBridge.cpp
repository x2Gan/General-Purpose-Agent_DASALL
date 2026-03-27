#include "linux/HalAvailabilityBridge.h"

#include <string_view>

#include "hal/HalProbe.h"

namespace dasall::platform::linux {

namespace {

bool profile_prefers_hal_probe(std::string_view profile_name) {
  return profile_name == "factory_test" || profile_name.rfind("edge_", 0) == 0;
}

}  // namespace

PlatformCapability HalAvailabilityBridge::probe_hal_availability(
    const PlatformInitConfig& config) const {
  if (!config.enable_hal) {
    return PlatformCapability::disabled("DisabledByProfile");
  }

  if (config.target_platform != PlatformInitConfig::kDefaultTargetPlatform) {
    return PlatformCapability::disabled("UnsupportedTargetPlatform");
  }

  const arm::hal::HalProbeResult result = arm::hal::probe_hal_availability();
  if (result.available) {
    return PlatformCapability::enabled();
  }

  if (profile_prefers_hal_probe(config.profile_name)) {
    return PlatformCapability::degraded(result.reason);
  }

  return PlatformCapability::disabled(result.reason);
}

}  // namespace dasall::platform::linux