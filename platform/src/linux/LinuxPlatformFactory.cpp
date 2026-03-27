#include "linux/LinuxPlatformFactory.h"

#include <array>
#include <string>

namespace dasall::platform::linux {

bool LinuxPlatformBundle::has_consistent_values() const {
  if (!config.has_consistent_values() || !capabilities.has_consistent_values()) {
    return false;
  }

  if (initialization_trace.size() < 3U) {
    return false;
  }

  return true;
}

PlatformResult<LinuxPlatformBundle> LinuxPlatformFactory::create(
    const PlatformInitConfig& config) const {
  if (!config.has_consistent_values()) {
    return PlatformResult<LinuxPlatformBundle>::failure(make_error(
        PlatformErrorCode::InvalidArgument,
        PlatformErrorCategory::Validation,
        "platform init config is inconsistent"));
  }

  std::vector<std::string> trace;
  trace.emplace_back("ProfileBound");

  const PlatformCapabilitySet detected_capabilities =
      detect_capabilities(config, &trace);
  trace.emplace_back("CapabilitiesResolved");

  if (!has_required_capabilities(detected_capabilities)) {
    return PlatformResult<LinuxPlatformBundle>::failure(
        make_error(PlatformErrorCode::ResourceExhausted,
                   PlatformErrorCategory::Resource,
                   "required platform capability is unavailable"));
  }

  trace.emplace_back("ReadyForServiceInit");

  LinuxPlatformBundle bundle;
  bundle.config = config;
  bundle.capabilities = detected_capabilities;
  bundle.initialization_trace = std::move(trace);

  if (!bundle.has_consistent_values()) {
    return PlatformResult<LinuxPlatformBundle>::failure(
        make_error(PlatformErrorCode::InternalFailure,
                   PlatformErrorCategory::Internal,
                   "factory produced inconsistent platform bundle"));
  }

  return PlatformResult<LinuxPlatformBundle>::success(std::move(bundle));
}

PlatformCapabilitySet LinuxPlatformFactory::detect_capabilities(
    const PlatformInitConfig& config,
    std::vector<std::string>* initialization_trace) const {
  PlatformCapabilitySet capabilities;

  if (initialization_trace != nullptr) {
    initialization_trace->emplace_back("ProbeCoreCapabilities");
  }

  const bool supports_linux = (config.target_platform == PlatformInitConfig::kDefaultTargetPlatform);

  if (supports_linux) {
    capabilities.thread = PlatformCapability::enabled();
    capabilities.timer = PlatformCapability::enabled();
    capabilities.queue = PlatformCapability::enabled();
    capabilities.filesystem = PlatformCapability::enabled();
    capabilities.network = PlatformCapability::enabled();
    capabilities.ipc = PlatformCapability::enabled();
  } else {
    capabilities.thread = PlatformCapability::disabled("UnsupportedTargetPlatform");
    capabilities.timer = PlatformCapability::disabled("UnsupportedTargetPlatform");
    capabilities.queue = PlatformCapability::disabled("UnsupportedTargetPlatform");
    capabilities.filesystem = PlatformCapability::disabled("UnsupportedTargetPlatform");
    capabilities.network = PlatformCapability::disabled("UnsupportedTargetPlatform");
    capabilities.ipc = PlatformCapability::disabled("UnsupportedTargetPlatform");
  }

  capabilities.hal = config.enable_hal ? PlatformCapability::enabled()
                                       : PlatformCapability::disabled("DisabledByProfile");
  return capabilities;
}

bool LinuxPlatformFactory::has_required_capabilities(
    const PlatformCapabilitySet& capabilities) const {
  const std::array<const PlatformCapability*, 6> required_capabilities = {
      &capabilities.thread,
      &capabilities.timer,
      &capabilities.queue,
      &capabilities.filesystem,
      &capabilities.network,
      &capabilities.ipc,
  };

  for (const PlatformCapability* capability : required_capabilities) {
    if (capability == nullptr || !capability->is_enabled()) {
      return false;
    }
  }

  return true;
}

PlatformError LinuxPlatformFactory::make_error(PlatformErrorCode code,
                                               PlatformErrorCategory category,
                                               std::string detail) const {
  return PlatformError{
      .code = code,
      .category = category,
      .retryable_hint = (code == PlatformErrorCode::Timeout),
      .syscall_name = {},
      .errno_value = std::nullopt,
      .detail = std::move(detail),
  };
}

}  // namespace dasall::platform::linux