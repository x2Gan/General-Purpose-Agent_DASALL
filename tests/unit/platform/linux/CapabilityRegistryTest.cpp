#include <exception>
#include <iostream>

#include "dasall/tests/support/TestAssertions.h"
#include "linux/CapabilityRegistry.h"

namespace {

void test_capability_registry_persists_capability_states_to_snapshot() {
  using dasall::platform::linux::CapabilityRegistry;
  using dasall::platform::linux::LinuxCapabilityKind;
  using dasall::platform::linux::PlatformCapability;
  using dasall::tests::support::assert_true;

  CapabilityRegistry registry;

  assert_true(registry.set_capability(LinuxCapabilityKind::Thread,
                                      PlatformCapability::enabled()),
              "set_capability should accept enabled state");
  assert_true(registry.set_capability(LinuxCapabilityKind::Network,
                                      PlatformCapability::degraded("EpollUnavailable")),
              "set_capability should accept degraded state with reason");
  assert_true(registry.set_capability(LinuxCapabilityKind::HAL,
                                      PlatformCapability::disabled("DisabledByProfile")),
              "set_capability should accept disabled state with reason");

  const auto snapshot = registry.snapshot();
  assert_true(snapshot.thread.is_enabled(), "snapshot should keep enabled thread capability");
  assert_true(snapshot.network.is_degraded(),
              "snapshot should keep degraded network capability");
  assert_true(snapshot.hal.is_disabled(), "snapshot should keep disabled hal capability");
}

void test_capability_registry_rejects_inconsistent_capability_values() {
  using dasall::platform::linux::CapabilityRegistry;
  using dasall::platform::linux::LinuxCapabilityKind;
  using dasall::platform::linux::PlatformCapability;
  using dasall::platform::linux::PlatformCapabilityState;
  using dasall::tests::support::assert_true;

  CapabilityRegistry registry;

  const PlatformCapability invalid_capability{
      .state = PlatformCapabilityState::Enabled,
      .reason = "UnexpectedReason",
  };

  assert_true(!registry.set_capability(LinuxCapabilityKind::Queue, invalid_capability),
              "set_capability should reject inconsistent capability value");

  const auto queue_capability = registry.get_capability(LinuxCapabilityKind::Queue);
  assert_true(queue_capability.has_value(), "registry should keep default queue capability entry");
  assert_true(queue_capability->is_disabled(),
              "invalid update should not replace default disabled state");
}

}  // namespace

int main() {
  try {
    test_capability_registry_persists_capability_states_to_snapshot();
    test_capability_registry_rejects_inconsistent_capability_values();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}