#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "diagnostics/DiagnosticsErrors.h"
#include "diagnostics/DiagnosticsServiceFacade.h"
#include "diagnostics/SnapshotStore.h"
#include "support/TestAssertions.h"

namespace {

dasall::infra::diagnostics::DiagnosticsSnapshot make_snapshot(std::string snapshot_id,
                                                              std::string collected_at) {
  return dasall::infra::diagnostics::DiagnosticsSnapshot{
      .snapshot_id = std::move(snapshot_id),
      .command = dasall::infra::diagnostics::DiagnosticsCommand{
          .command_id = std::string("diag-snapshot-store-cmd"),
          .command_name = std::string("health.snapshot"),
          .args = {std::string("--summary")},
          .request_scope = std::string("runtime"),
          .timeout_ms = 3000,
          .actor_ref = std::string("actor://redacted"),
      },
      .collected_at = std::move(collected_at),
      .summary = std::string("diagnostics redacted health snapshot"),
      .evidence_refs = {std::string("logs://diagnostics/health.snapshot"),
                        std::string("metrics://diagnostics/health.snapshot"),
                        std::string("health://diagnostics/health.snapshot"),
                        std::string("errors://diagnostics/health.snapshot"),
                        std::string("command://diagnostics/health.snapshot")},
      .redaction_profile = dasall::infra::diagnostics::RedactionProfile::Strict,
      .exporter_hint = std::string("local_file"),
  };
}

void test_snapshot_store_persists_and_reads_back_redacted_snapshots() {
  using dasall::infra::diagnostics::SnapshotStore;
  using dasall::tests::support::assert_true;

  SnapshotStore store;
  store.inject_current_time_for_test("2026-04-07T21:00:00Z");

  const auto store_result =
      store.store(make_snapshot("diag-snapshot-store-001", "2026-04-07T20:00:00Z"));
  assert_true(store_result.stored && store_result.snapshot_id == "diag-snapshot-store-001",
              "SnapshotStore should persist a valid redacted diagnostics snapshot and return its snapshot_id");

  const auto stored_snapshot = store.get("diag-snapshot-store-001");
  assert_true(stored_snapshot.has_value() && stored_snapshot->snapshot_id == "diag-snapshot-store-001" &&
                  stored_snapshot->command.actor_ref == "actor://redacted",
              "SnapshotStore should read back the retained redacted snapshot without mutating the stored actor anchor");
}

void test_snapshot_store_trims_oldest_snapshot_when_max_count_is_exceeded() {
  using dasall::infra::diagnostics::SnapshotStore;
  using dasall::infra::diagnostics::SnapshotStoreOptions;
  using dasall::tests::support::assert_true;

  SnapshotStore store(SnapshotStoreOptions{.retention_days = 7, .max_snapshot_count = 2});
  store.inject_current_time_for_test("2026-04-07T23:00:00Z");

  assert_true(store.store(make_snapshot("diag-snapshot-store-002", "2026-04-07T20:00:00Z")).stored,
              "SnapshotStore should accept the first retained snapshot in a bounded store");
  assert_true(store.store(make_snapshot("diag-snapshot-store-003", "2026-04-07T21:00:00Z")).stored,
              "SnapshotStore should accept the second retained snapshot in a bounded store");
  assert_true(store.store(make_snapshot("diag-snapshot-store-004", "2026-04-07T22:00:00Z")).stored,
              "SnapshotStore should accept the third retained snapshot before trimming the oldest entry");

  assert_true(!store.contains("diag-snapshot-store-002") &&
                  store.contains("diag-snapshot-store-003") &&
                  store.contains("diag-snapshot-store-004"),
              "SnapshotStore should evict the oldest retained snapshot once max_snapshot_count is exceeded");
}

void test_snapshot_store_prunes_snapshots_outside_the_retention_window() {
  using dasall::infra::diagnostics::SnapshotStore;
  using dasall::infra::diagnostics::SnapshotStoreOptions;
  using dasall::tests::support::assert_true;

  SnapshotStore store(SnapshotStoreOptions{.retention_days = 1, .max_snapshot_count = 8});
  store.inject_current_time_for_test("2026-04-08T10:00:00Z");

  assert_true(store.store(make_snapshot("diag-snapshot-store-old", "2026-04-07T10:00:00Z")).stored,
              "SnapshotStore should accept an older retained snapshot before retention pruning is evaluated against a later clock");
  assert_true(store.store(make_snapshot("diag-snapshot-store-fresh", "2026-04-08T10:00:02Z")).stored,
              "SnapshotStore should accept a fresh retained snapshot before retention pruning is evaluated against a later clock");

  store.inject_current_time_for_test("2026-04-09T10:00:01Z");

  assert_true(!store.contains("diag-snapshot-store-old") &&
                  store.contains("diag-snapshot-store-fresh"),
              "SnapshotStore should prune snapshots whose collected_at falls outside the configured retention_days window");
}

void test_snapshot_store_and_facade_surface_snapshot_persistence_failures() {
  using dasall::infra::diagnostics::DiagnosticsCommand;
  using dasall::infra::diagnostics::DiagnosticsErrorCode;
  using dasall::infra::diagnostics::DiagnosticsServiceFacade;
  using dasall::infra::diagnostics::SnapshotStore;
  using dasall::infra::diagnostics::map_diagnostics_error_code;
  using dasall::tests::support::assert_true;

  SnapshotStore store;
  store.inject_current_time_for_test("2026-04-07T21:30:01Z");
  const auto invalid_result = store.store(dasall::infra::diagnostics::DiagnosticsSnapshot{});
  assert_true(!invalid_result.stored && invalid_result.references_only_contract_error_types(),
              "SnapshotStore should reject structurally invalid snapshots through the frozen contracts error boundary");
  assert_true(invalid_result.result_code ==
                  map_diagnostics_error_code(DiagnosticsErrorCode::SnapshotStoreFail).result_code,
              "SnapshotStore should map invalid persistence attempts to INF_E_DIAG_SNAPSHOT_STORE_FAIL");

  assert_true(store.store(make_snapshot("diag-snapshot-store-005", "2026-04-07T20:30:00Z")).stored,
              "SnapshotStore should accept a valid retained snapshot before injected failure coverage");

  store.inject_commit_failure_for_test("simulated diagnostics snapshot store failure");
  const auto injected_failure =
      store.store(make_snapshot("diag-snapshot-store-006", "2026-04-07T21:30:00Z"));
  assert_true(!injected_failure.stored && injected_failure.references_only_contract_error_types(),
              "SnapshotStore should surface injected persistence failures without leaving the contracts error boundary");
  assert_true(injected_failure.error.has_value() &&
                  injected_failure.error->details.message.find("simulated diagnostics snapshot store failure") !=
                      std::string::npos,
              "SnapshotStore should preserve injected failure reasons for observability");
  assert_true(store.contains("diag-snapshot-store-005") && !store.contains("diag-snapshot-store-006"),
              "SnapshotStore should keep previously retained snapshots stable and avoid recording failed commits");

  DiagnosticsServiceFacade service;
  assert_true(service.start(),
              "DiagnosticsServiceFacade should start before exercising snapshot persistence failures through execute");
  service.inject_snapshot_store_commit_failure_for_test(
      "simulated facade snapshot store failure");

  const auto execute_result = service.execute(DiagnosticsCommand{
      .command_id = std::string("diag-store-failure-cmd"),
      .command_name = std::string("health.snapshot"),
      .args = {},
      .request_scope = std::string("runtime"),
      .timeout_ms = 3000,
      .actor_ref = std::string("ops-user"),
  });

  assert_true(!execute_result.ok && execute_result.references_only_contract_error_types(),
              "DiagnosticsServiceFacade should surface snapshot persistence failures when SnapshotStore rejects a commit");
  assert_true(execute_result.result_code ==
                  map_diagnostics_error_code(DiagnosticsErrorCode::SnapshotStoreFail).result_code,
              "DiagnosticsServiceFacade should map snapshot persistence failures to INF_E_DIAG_SNAPSHOT_STORE_FAIL");
  assert_true(execute_result.error.has_value() &&
                  execute_result.error->details.message.find("simulated facade snapshot store failure") !=
                      std::string::npos,
              "DiagnosticsServiceFacade should preserve SnapshotStore failure reasons when execute cannot persist a redacted snapshot");
}

}  // namespace

int main() {
  try {
    test_snapshot_store_persists_and_reads_back_redacted_snapshots();
    test_snapshot_store_trims_oldest_snapshot_when_max_count_is_exceeded();
    test_snapshot_store_prunes_snapshots_outside_the_retention_window();
    test_snapshot_store_and_facade_surface_snapshot_persistence_failures();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}