#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "checkpoint/RecoveryOutcome.h"
#include "../../../infra/include/HealthSnapshot.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_health_snapshot_keeps_top_level_state_inside_infra_boundary() {
  using dasall::infra::HealthSnapshot;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(HealthSnapshot{}.liveness), bool>);
  static_assert(std::is_same_v<decltype(HealthSnapshot{}.readiness), bool>);
  static_assert(std::is_same_v<decltype(HealthSnapshot{}.degraded), bool>);
  static_assert(std::is_same_v<decltype(HealthSnapshot{}.failed_components),
                               HealthSnapshot::ComponentList>);

  const HealthSnapshot snapshot{
      .liveness = true,
      .readiness = false,
      .degraded = true,
      .failed_components = {"ota_manager"},
  };

  assert_true(snapshot.has_consistent_state(),
              "health snapshot should remain an infra-owned state summary rather than a runtime-state object");
}

void test_health_snapshot_rejects_runtime_state_field_names_in_failed_components() {
  using dasall::contracts::RecoveryOutcome;
  using dasall::infra::HealthSnapshot;
  using dasall::tests::support::assert_true;

  const RecoveryOutcome outcome{
      .executed_action = std::string("rollback"),
      .final_runtime_state = std::string("degraded"),
      .updated_retry_count = 2,
      .checkpoint_ref = std::string("checkpoint-001"),
      .compensation_result_ref = std::string("comp-001"),
      .rejection_reason = std::nullopt,
      .escalation_reason = std::nullopt,
  };

  const HealthSnapshot invalid_snapshot{
      .liveness = true,
      .readiness = false,
      .degraded = true,
      .failed_components = {*outcome.final_runtime_state, "final_runtime_state"},
  };

  assert_true(invalid_snapshot.failed_components_are_valid() == false,
              "health snapshot should reject runtime-state field names as failed component identifiers");
  assert_true(HealthSnapshot::is_reserved_runtime_state_name("final_runtime_state"),
              "runtime state field names must remain reserved outside HealthSnapshot");
}

}  // namespace

int main() {
  try {
    test_health_snapshot_keeps_top_level_state_inside_infra_boundary();
    test_health_snapshot_rejects_runtime_state_field_names_in_failed_components();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}