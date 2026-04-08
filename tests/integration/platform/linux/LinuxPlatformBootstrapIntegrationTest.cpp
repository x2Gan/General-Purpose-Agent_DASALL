#include <algorithm>
#include <exception>
#include <iostream>

#include "support/TestAssertions.h"
#include "linux/LinuxPlatformCapabilities.h"
#include "linux/LinuxPlatformFactory.h"

namespace {

void test_linux_platform_bootstrap_desktop_profile_is_discoverable_and_ready() {
  using dasall::platform::linux::LinuxPlatformFactory;
  using dasall::platform::linux::PlatformInitConfig;
  using dasall::tests::support::assert_true;

  LinuxPlatformFactory factory;
  PlatformInitConfig config;
  config.target_platform = "linux";
  config.profile_name = "desktop_full";
  config.enable_hal = false;

  const auto result = factory.create(config);
  assert_true(result.ok(), "desktop_full bootstrap should succeed");
  assert_true(result.value->has_consistent_values(), "bootstrap bundle should stay consistent");

  const auto& trace = result.value->initialization_trace;
  const auto profile_it = std::find(trace.begin(), trace.end(), "ProfileBound");
  const auto ready_it = std::find(trace.begin(), trace.end(), "ReadyForServiceInit");
  assert_true(profile_it != trace.end(), "trace should contain ProfileBound");
  assert_true(ready_it != trace.end(), "trace should contain ReadyForServiceInit");
  assert_true(profile_it < ready_it, "ProfileBound should precede ReadyForServiceInit");

  assert_true(result.value->capabilities.hal.is_disabled(),
              "desktop_full with enable_hal=false should have HAL disabled");
}

void test_linux_platform_bootstrap_edge_balanced_hal_degrades_to_stub() {
  using dasall::platform::linux::LinuxPlatformFactory;
  using dasall::platform::linux::PlatformCapabilityState;
  using dasall::platform::linux::PlatformInitConfig;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  LinuxPlatformFactory factory;
  PlatformInitConfig config;
  config.target_platform = "linux";
  config.profile_name = "edge_balanced";
  config.enable_hal = true;

  const auto result = factory.create(config);
  assert_true(result.ok(),
              "edge_balanced bootstrap should succeed even when HAL probes return stub-only");
  assert_true(result.value->has_consistent_values(),
              "edge_balanced bootstrap bundle should stay consistent");

  const auto& trace = result.value->initialization_trace;
  const auto profile_it = std::find(trace.begin(), trace.end(), "ProfileBound");
  const auto ready_it = std::find(trace.begin(), trace.end(), "ReadyForServiceInit");
  assert_true(profile_it != trace.end(), "trace should contain ProfileBound");
  assert_true(ready_it != trace.end(), "trace should contain ReadyForServiceInit");
  assert_true(profile_it < ready_it, "ProfileBound should precede ReadyForServiceInit");

  assert_true(result.value->capabilities.hal.is_degraded(),
              "edge_balanced with enable_hal=true should degrade HAL to stub-only");
  assert_equal("HalStubOnly", result.value->capabilities.hal.reason,
               "edge_balanced HAL degraded reason should be HalStubOnly");
}

}  // namespace

int main() {
  try {
    test_linux_platform_bootstrap_desktop_profile_is_discoverable_and_ready();
    test_linux_platform_bootstrap_edge_balanced_hal_degrades_to_stub();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
