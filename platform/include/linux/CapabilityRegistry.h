#pragma once

#include <optional>

#include "linux/LinuxPlatformCapabilities.h"

namespace dasall::platform::linux {

enum class LinuxCapabilityKind {
  Thread,
  Timer,
  Queue,
  FileSystem,
  Network,
  IPC,
  HAL,
};

class CapabilityRegistry {
 public:
  [[nodiscard]] bool set_capability(LinuxCapabilityKind kind,
                                    const PlatformCapability& capability);
  [[nodiscard]] std::optional<PlatformCapability> get_capability(
      LinuxCapabilityKind kind) const;
  [[nodiscard]] PlatformCapabilitySet snapshot() const;

 private:
  PlatformCapabilitySet capabilities_{};
};

}  // namespace dasall::platform::linux