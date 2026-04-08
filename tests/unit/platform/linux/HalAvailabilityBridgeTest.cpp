#include <exception>
#include <iostream>

#include "support/TestAssertions.h"
#include "linux/HalAvailabilityBridge.h"

namespace {

void test_hal_availability_bridge_disables_hal_for_desktop_profile() {
  using dasall::platform::linux::HalAvailabilityBridge;
  using dasall::platform::linux::PlatformInitConfig;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  HalAvailabilityBridge bridge;
  PlatformInitConfig config;
  config.target_platform = "linux";
  config.profile_name = "desktop_full";
  config.enable_hal = false;

  const auto capability = bridge.probe_hal_availability(config);
  assert_true(capability.is_disabled(), "desktop profile should keep HAL disabled");
  assert_equal("DisabledByProfile", capability.reason,
               "desktop path should expose DisabledByProfile reason");
  assert_true(capability.has_consistent_values(),
              "desktop HAL capability should remain internally consistent");
}

void test_hal_availability_bridge_reports_edge_hal_path_as_deterministic_degraded() {
  using dasall::platform::linux::HalAvailabilityBridge;
  using dasall::platform::linux::PlatformInitConfig;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  HalAvailabilityBridge bridge;
  PlatformInitConfig config;
  config.target_platform = "linux";
  config.profile_name = "edge_balanced";
  config.enable_hal = true;

  const auto capability = bridge.probe_hal_availability(config);
  assert_true(capability.is_degraded(),
              "edge profile should return deterministic degraded HAL path on stub-only build");
  assert_equal("HalStubOnly", capability.reason,
               "stub probe reason should surface as HalStubOnly");
  assert_true(capability.has_consistent_values(),
              "edge HAL capability should remain internally consistent");
}

}  // namespace

int main() {
  try {
    test_hal_availability_bridge_disables_hal_for_desktop_profile();
    test_hal_availability_bridge_reports_edge_hal_path_as_deterministic_degraded();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}