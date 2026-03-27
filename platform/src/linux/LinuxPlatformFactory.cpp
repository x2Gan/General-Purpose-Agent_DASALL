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

  CapabilityRegistry registry;
  const bool registry_ok =
      registry.set_capability(LinuxCapabilityKind::Thread, detected_capabilities.thread) &&
      registry.set_capability(LinuxCapabilityKind::Timer, detected_capabilities.timer) &&
      registry.set_capability(LinuxCapabilityKind::Queue, detected_capabilities.queue) &&
      registry.set_capability(LinuxCapabilityKind::FileSystem,
                              detected_capabilities.filesystem) &&
      registry.set_capability(LinuxCapabilityKind::Network, detected_capabilities.network) &&
      registry.set_capability(LinuxCapabilityKind::IPC, detected_capabilities.ipc) &&
      registry.set_capability(LinuxCapabilityKind::HAL, detected_capabilities.hal);

  if (!registry_ok) {
    return PlatformResult<LinuxPlatformBundle>::failure(
        make_error(PlatformErrorCode::InternalFailure,
                   PlatformErrorCategory::Internal,
                   "capability registry rejected detected capability state"));
  }

  const std::array<LinuxCapabilityKind, 6> required_capabilities = {
      LinuxCapabilityKind::Thread,
      LinuxCapabilityKind::Timer,
      LinuxCapabilityKind::Queue,
      LinuxCapabilityKind::FileSystem,
      LinuxCapabilityKind::Network,
      LinuxCapabilityKind::IPC,
  };

  for (const LinuxCapabilityKind kind : required_capabilities) {
    const std::optional<PlatformCapability> capability = registry.get_capability(kind);
    if (!capability.has_value() || !capability->is_enabled()) {
      return PlatformResult<LinuxPlatformBundle>::failure(
          make_error(PlatformErrorCode::ResourceExhausted,
                     PlatformErrorCategory::Resource,
                     "required platform capability is unavailable"));
    }
  }

  trace.emplace_back("ReadyForServiceInit");

  LinuxPlatformBundle bundle;
  bundle.config = config;
  bundle.capabilities = registry.snapshot();
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