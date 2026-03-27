#include "hal/HalProbe.h"

namespace dasall::platform::arm::hal {

HalProbeResult probe_hal_availability() {
  return HalProbeResult{
      .available = false,
      .reason = "HalStubOnly",
  };
}

}  // namespace dasall::platform::arm::hal