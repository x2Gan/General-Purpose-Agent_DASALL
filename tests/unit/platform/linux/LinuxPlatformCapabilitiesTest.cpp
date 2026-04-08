#include <exception>
#include <iostream>

#include "support/TestAssertions.h"
#include "linux/LinuxPlatformCapabilities.h"

namespace {

void test_platform_capability_set_defaults_to_not_probed_disabled_capabilities() {
  using dasall::platform::linux::PlatformCapability;
  using dasall::platform::linux::PlatformCapabilitySet;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const PlatformCapabilitySet capability_set;

  assert_true(capability_set.thread.is_disabled(), "thread capability should default to disabled");
  assert_true(capability_set.timer.is_disabled(), "timer capability should default to disabled");
  assert_true(capability_set.queue.is_disabled(), "queue capability should default to disabled");
  assert_true(capability_set.filesystem.is_disabled(),
              "filesystem capability should default to disabled");
  assert_true(capability_set.network.is_disabled(), "network capability should default to disabled");
  assert_true(capability_set.ipc.is_disabled(), "ipc capability should default to disabled");
  assert_true(capability_set.hal.is_disabled(), "hal capability should default to disabled");
  assert_equal("NotProbed", capability_set.thread.reason,
               "default disabled capability should preserve not-probed reason");
  assert_true(capability_set.has_consistent_values(),
              "default capability set should remain internally consistent");
  assert_true(capability_set.degraded_count() == 0,
              "default capability set should not report degraded capabilities");
  assert_equal("NotProbed", std::string(PlatformCapability::kReasonNotProbed),
               "reason token constant should stay stable");
}

void test_platform_capability_set_tracks_enabled_disabled_and_degraded_states() {
  using dasall::platform::linux::PlatformCapability;
  using dasall::platform::linux::PlatformCapabilitySet;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  PlatformCapabilitySet capability_set;
  capability_set.thread = PlatformCapability::enabled();
  capability_set.timer = PlatformCapability::enabled();
  capability_set.queue = PlatformCapability::degraded("QueuePressure");
  capability_set.filesystem = PlatformCapability::enabled();
  capability_set.network = PlatformCapability::degraded("EpollUnavailable");
  capability_set.ipc = PlatformCapability::enabled();
  capability_set.hal = PlatformCapability::disabled("DisabledByProfile");

  assert_true(capability_set.thread.is_enabled(), "thread capability should support enabled state");
  assert_true(capability_set.queue.is_degraded(), "queue capability should support degraded state");
  assert_true(capability_set.hal.is_disabled(), "hal capability should support disabled state");
  assert_equal("QueuePressure", capability_set.queue.reason,
               "degraded capability should preserve reason text");
  assert_equal("DisabledByProfile", capability_set.hal.reason,
               "disabled capability should preserve reason text");
  assert_true(capability_set.has_consistent_values(),
              "mixed capability states should remain valid when reasons are coherent");
  assert_true(capability_set.degraded_count() == 2,
              "degraded_count should report only degraded capabilities");
}

void test_platform_capability_rejects_reason_mismatches() {
  using dasall::platform::linux::PlatformCapability;
  using dasall::platform::linux::PlatformCapabilitySet;
  using dasall::platform::linux::PlatformCapabilityState;
  using dasall::tests::support::assert_true;

  const PlatformCapability enabled_with_reason{
      .state = PlatformCapabilityState::Enabled,
      .reason = "UnexpectedReason",
  };

  const PlatformCapability degraded_without_reason{
      .state = PlatformCapabilityState::Degraded,
      .reason = {},
  };

  PlatformCapabilitySet invalid_set;
  invalid_set.network = degraded_without_reason;

  assert_true(!enabled_with_reason.has_consistent_values(),
              "enabled capability should not carry a degradation reason");
  assert_true(!degraded_without_reason.has_consistent_values(),
              "degraded capability should require a non-empty reason");
  assert_true(!invalid_set.has_consistent_values(),
              "capability set should reject inconsistent capability entries");
}

}  // namespace

int main() {
  try {
    test_platform_capability_set_defaults_to_not_probed_disabled_capabilities();
    test_platform_capability_set_tracks_enabled_disabled_and_degraded_states();
    test_platform_capability_rejects_reason_mismatches();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}