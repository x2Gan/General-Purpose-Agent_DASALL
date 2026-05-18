#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "MemoryMaintenanceProofRunner.h"
#include "support/TestAssertions.h"

namespace {

std::filesystem::path make_temp_state_root() {
  const auto timestamp = std::to_string(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
  return std::filesystem::temp_directory_path() /
         ("dasall-memory-maintenance-proof-test-" + timestamp);
}

void test_memory_maintenance_proof_runner_collects_local_state_evidence() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto state_root = make_temp_state_root();
  std::filesystem::create_directories(state_root);

  const auto cleanup = std::shared_ptr<void>(nullptr, [&](void*) {
    std::error_code error;
    std::filesystem::remove_all(state_root, error);
  });
  (void)cleanup;

  const auto result = dasall::apps::daemon::collect_memory_maintenance_proof(
      dasall::apps::daemon::MemoryMaintenanceProofOptions{
          .requested_profile_id = "edge_minimal",
          .deployment_config_path = std::nullopt,
          .state_root_override = state_root,
      });

  assert_true(result.ok(), "maintenance proof runner should succeed on a temp state root");
  assert_equal(result.retention_turns + 1,
               result.turns_before,
               "proof runner should seed exactly one turn above the retention window");
  assert_equal(result.retention_turns,
               result.turns_after,
               "proof runner should converge turn count back to the retention window");
  assert_equal(1,
               result.quarantine_rows_before,
               "proof runner should seed one expired quarantine row");
  assert_equal(0,
               result.quarantine_rows_after,
               "proof runner should remove the expired quarantine row");
  assert_true(result.protected_turn_retained,
              "proof runner should retain the summary-protected oldest turn");
  assert_true(result.purged_turn_removed,
              "proof runner should purge the first unprotected over-budget turn");
  assert_true(result.newest_turn_retained,
              "proof runner should retain the newest turn inside the retention window");
  assert_true(result.maintenance_report.checkpoint_executed,
              "proof runner should execute a checkpoint on the local WAL");
  assert_equal(0,
               result.maintenance_report.checkpoint_wal_pages_remaining,
               "proof runner should drain WAL pages when no busy reader exists");
  assert_true(result.maintenance_report.turns_purged >= 1,
              "proof runner should report at least one purged turn");
  assert_true(result.maintenance_report.quarantine_cleaned >= 1,
              "proof runner should report quarantine cleanup");
  assert_equal(std::string("wal"),
               result.journal_mode,
               "proof runner should operate on a WAL-backed sqlite database");
  assert_true(result.wal_bytes_before > 0,
              "proof runner should observe WAL bytes before checkpointing");
}

}  // namespace

int main() {
  try {
    test_memory_maintenance_proof_runner_collects_local_state_evidence();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}