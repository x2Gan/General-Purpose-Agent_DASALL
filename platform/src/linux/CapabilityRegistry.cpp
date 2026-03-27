#include "linux/CapabilityRegistry.h"

namespace dasall::platform::linux {

namespace {

PlatformCapability* pick_mutable_capability(PlatformCapabilitySet* set,
                                            LinuxCapabilityKind kind) {
  switch (kind) {
    case LinuxCapabilityKind::Thread:
      return &set->thread;
    case LinuxCapabilityKind::Timer:
      return &set->timer;
    case LinuxCapabilityKind::Queue:
      return &set->queue;
    case LinuxCapabilityKind::FileSystem:
      return &set->filesystem;
    case LinuxCapabilityKind::Network:
      return &set->network;
    case LinuxCapabilityKind::IPC:
      return &set->ipc;
    case LinuxCapabilityKind::HAL:
      return &set->hal;
  }

  return nullptr;
}

const PlatformCapability* pick_capability(const PlatformCapabilitySet& set,
                                          LinuxCapabilityKind kind) {
  switch (kind) {
    case LinuxCapabilityKind::Thread:
      return &set.thread;
    case LinuxCapabilityKind::Timer:
      return &set.timer;
    case LinuxCapabilityKind::Queue:
      return &set.queue;
    case LinuxCapabilityKind::FileSystem:
      return &set.filesystem;
    case LinuxCapabilityKind::Network:
      return &set.network;
    case LinuxCapabilityKind::IPC:
      return &set.ipc;
    case LinuxCapabilityKind::HAL:
      return &set.hal;
  }

  return nullptr;
}

}  // namespace

bool CapabilityRegistry::set_capability(LinuxCapabilityKind kind,
                                        const PlatformCapability& capability) {
  PlatformCapability* target = pick_mutable_capability(&capabilities_, kind);
  if (target == nullptr || !capability.has_consistent_values()) {
    return false;
  }

  *target = capability;
  return true;
}

std::optional<PlatformCapability> CapabilityRegistry::get_capability(
    LinuxCapabilityKind kind) const {
  const PlatformCapability* capability = pick_capability(capabilities_, kind);
  if (capability == nullptr) {
    return std::nullopt;
  }

  return *capability;
}

PlatformCapabilitySet CapabilityRegistry::snapshot() const {
  return capabilities_;
}

}  // namespace dasall::platform::linux