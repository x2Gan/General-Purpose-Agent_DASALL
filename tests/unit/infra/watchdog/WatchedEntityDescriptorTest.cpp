#include <exception>
#include <iostream>
#include <string>

#include "watchdog/WatchedEntityDescriptor.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_watched_entity_descriptor_accepts_complete_private_watchdog_shape() {
  using dasall::infra::watchdog::WatchedEntityDescriptor;
  using dasall::infra::watchdog::WatchdogEntityCriticality;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const WatchedEntityDescriptor descriptor{
      .entity_id = std::string("runtime.main_loop"),
      .entity_type = std::string("thread"),
      .owner_module = std::string("runtime"),
      .criticality = WatchdogEntityCriticality::Critical,
      .timeout_ms = 15000,
      .grace_ms = 2000,
  };

  assert_true(descriptor.has_required_fields(),
              "WatchedEntityDescriptor should require all frozen fields before registration");
  assert_equal(std::string("runtime.main_loop"), descriptor.uniqueness_key(),
               "WatchedEntityDescriptor uniqueness pre-check should key on entity_id");
}

void test_watched_entity_descriptor_rejects_incomplete_or_duplicate_identity_inputs() {
  using dasall::infra::watchdog::WatchedEntityDescriptor;
  using dasall::infra::watchdog::WatchdogEntityCriticality;
  using dasall::tests::support::assert_true;

  const WatchedEntityDescriptor incomplete{};
  const WatchedEntityDescriptor primary{
      .entity_id = std::string("runtime.main_loop"),
      .entity_type = std::string("thread"),
      .owner_module = std::string("runtime"),
      .criticality = WatchdogEntityCriticality::Critical,
      .timeout_ms = 15000,
      .grace_ms = 2000,
  };
  const WatchedEntityDescriptor duplicate{
      .entity_id = std::string("runtime.main_loop"),
      .entity_type = std::string("task_executor"),
      .owner_module = std::string("runtime"),
      .criticality = WatchdogEntityCriticality::NonCritical,
      .timeout_ms = 5000,
      .grace_ms = 500,
  };
  const WatchedEntityDescriptor invalid_timing{
      .entity_id = std::string("runtime.bg_worker"),
      .entity_type = std::string("thread"),
      .owner_module = std::string("runtime"),
      .criticality = WatchdogEntityCriticality::Critical,
      .timeout_ms = 1000,
      .grace_ms = 1000,
  };

  assert_true(!incomplete.has_required_fields(),
              "WatchedEntityDescriptor should reject empty required fields and unspecified criticality");
  assert_true(primary.reuses_entity_id_of(duplicate),
              "WatchedEntityDescriptor should surface duplicate entity ids before registry logic is implemented");
  assert_true(!invalid_timing.has_required_fields(),
              "WatchedEntityDescriptor should reject grace_ms values that collapse the timeout window");
}

}  // namespace

int main() {
  try {
    test_watched_entity_descriptor_accepts_complete_private_watchdog_shape();
    test_watched_entity_descriptor_rejects_incomplete_or_duplicate_identity_inputs();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}