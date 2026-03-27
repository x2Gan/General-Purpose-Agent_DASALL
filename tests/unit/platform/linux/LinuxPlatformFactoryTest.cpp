#include <algorithm>
#include <exception>
#include <iostream>

#include "dasall/tests/support/TestAssertions.h"
#include "linux/LinuxPlatformFactory.h"

namespace {

void test_linux_platform_factory_creates_bundle_with_profile_bound_before_service_init() {
  using dasall::platform::linux::LinuxPlatformFactory;
  using dasall::platform::linux::PlatformInitConfig;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  LinuxPlatformFactory factory;
  PlatformInitConfig config;
  config.target_platform = "linux";
  config.profile_name = "desktop_full";
  config.enable_hal = false;

  const auto result = factory.create(config);
  assert_true(result.ok(), "factory create should succeed on baseline linux profile");
  assert_true(result.value->has_consistent_values(),
              "factory should return internally consistent bundle");

  const auto& trace = result.value->initialization_trace;
  const auto profile_it = std::find(trace.begin(), trace.end(), "ProfileBound");
  const auto ready_it = std::find(trace.begin(), trace.end(), "ReadyForServiceInit");
  assert_true(profile_it != trace.end(), "initialization trace should include ProfileBound");
  assert_true(ready_it != trace.end(),
              "initialization trace should include ReadyForServiceInit");
  assert_true(profile_it < ready_it,
              "ProfileBound should happen before service init readiness");
  assert_equal("DisabledByProfile", result.value->capabilities.hal.reason,
               "desktop profile should keep HAL disabled by profile reason");
}

void test_linux_platform_factory_blocks_when_required_capability_is_unavailable() {
  using dasall::platform::PlatformErrorCategory;
  using dasall::platform::PlatformErrorCode;
  using dasall::platform::linux::LinuxPlatformFactory;
  using dasall::platform::linux::PlatformInitConfig;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  LinuxPlatformFactory factory;
  PlatformInitConfig config;
  config.target_platform = "unsupported-target";

  const auto result = factory.create(config);
  assert_true(!result.ok(),
              "factory create should fail when required linux capabilities are unavailable");
  assert_true(result.error.has_value(), "failure path should include platform error detail");
  assert_true(result.error->has_consistent_values(),
              "error payload should remain internally consistent");
  assert_true(result.error->code == PlatformErrorCode::ResourceExhausted,
              "missing required capability should map to ResourceExhausted");
  assert_true(result.error->category == PlatformErrorCategory::Resource,
              "missing required capability should map to Resource category");
  assert_equal("required platform capability is unavailable", result.error->detail,
               "error detail should identify required capability gate");
}

}  // namespace

int main() {
  try {
    test_linux_platform_factory_creates_bundle_with_profile_bound_before_service_init();
    test_linux_platform_factory_blocks_when_required_capability_is_unavailable();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}