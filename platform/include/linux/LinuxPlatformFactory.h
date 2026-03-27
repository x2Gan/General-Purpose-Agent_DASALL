#pragma once

#include <memory>
#include <string>
#include <vector>

#include "IFileSystem.h"
#include "IIPC.h"
#include "INetwork.h"
#include "IQueue.h"
#include "IThread.h"
#include "ITimer.h"
#include "PlatformResult.h"
#include "linux/CapabilityRegistry.h"
#include "linux/LinuxPlatformCapabilities.h"
#include "linux/PlatformInitConfig.h"

namespace dasall::platform::linux {

struct LinuxPlatformBundle {
  PlatformInitConfig config;
  PlatformCapabilitySet capabilities;
  std::shared_ptr<IThread> thread;
  std::shared_ptr<ITimer> timer;
  std::shared_ptr<IQueue> queue;
  std::shared_ptr<IFileSystem> filesystem;
  std::shared_ptr<INetwork> network;
  std::shared_ptr<IIPC> ipc;
  std::vector<std::string> initialization_trace;

  [[nodiscard]] bool has_consistent_values() const;
};

class LinuxPlatformFactory {
 public:
  LinuxPlatformFactory() = default;

  [[nodiscard]] PlatformResult<LinuxPlatformBundle> create(
      const PlatformInitConfig& config) const;

 private:
  [[nodiscard]] PlatformCapabilitySet detect_capabilities(
      const PlatformInitConfig& config,
      std::vector<std::string>* initialization_trace) const;
  [[nodiscard]] PlatformError make_error(PlatformErrorCode code,
                                         PlatformErrorCategory category,
                                         std::string detail) const;
};

}  // namespace dasall::platform::linux