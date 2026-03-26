#include <exception>
#include <iostream>

#include "dasall/tests/support/TestAssertions.h"
#include "linux/PlatformInitConfig.h"

namespace {

void test_platform_init_config_defaults_match_linux_design_baseline() {
  using dasall::platform::linux::PlatformInitConfig;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const PlatformInitConfig config;

  assert_equal("linux", config.target_platform,
               "default target_platform should match linux platform baseline");
  assert_equal("desktop_full", config.profile_name,
               "default profile_name should match desktop profile baseline");
  assert_true(!config.enable_hal, "default enable_hal should be disabled on desktop baseline");
  assert_true(config.queue_defaults.capacity == 1024,
              "default queue capacity should match design baseline");
  assert_equal("reject", config.queue_defaults.overflow_policy,
               "default queue overflow policy should match design baseline");
  assert_true(config.io_timeouts.connect_timeout_ms == 3000,
              "default connect timeout should match design baseline");
  assert_true(config.io_timeouts.io_timeout_ms == 5000,
              "default io timeout should match design baseline");
  assert_true(config.has_consistent_values(),
              "default PlatformInitConfig should be internally consistent");
}

void test_platform_init_config_rejects_incomplete_or_invalid_values() {
  using dasall::platform::linux::PlatformInitConfig;
  using dasall::tests::support::assert_true;

  PlatformInitConfig missing_profile;
  missing_profile.profile_name.clear();

  PlatformInitConfig invalid_queue;
  invalid_queue.queue_defaults.capacity = 0;

  PlatformInitConfig invalid_timeout;
  invalid_timeout.io_timeouts.io_timeout_ms = -1;

  assert_true(!missing_profile.has_consistent_values(),
              "empty profile_name should be rejected");
  assert_true(!invalid_queue.has_consistent_values(),
              "zero queue capacity should be rejected");
  assert_true(!invalid_timeout.has_consistent_values(),
              "negative io timeout should be rejected");
}

}  // namespace

int main() {
  try {
    test_platform_init_config_defaults_match_linux_design_baseline();
    test_platform_init_config_rejects_incomplete_or_invalid_values();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}