#include <exception>
#include <iostream>
#include <string>

#include "DiagnosticsSnapshotFixture.h"
#include "support/TestAssertions.h"

namespace {

void test_diagnostics_fixture_freezes_retained_snapshot_surface() {
  using dasall::tests::fixtures::infra::kDiagnosticsRetainedActorRef;
  using dasall::tests::fixtures::infra::kDiagnosticsRetainedSummary;
  using dasall::tests::fixtures::infra::make_execute_command_fixture;
  using dasall::tests::fixtures::infra::make_retained_snapshot_fixture;
  using dasall::tests::fixtures::infra::matches_retained_snapshot_contract;
  using dasall::tests::support::assert_true;

  const auto execute_command = make_execute_command_fixture();
  assert_true(execute_command.is_read_only_whitelisted(),
              "diagnostics fixture should keep execute commands inside the frozen read-only whitelist");

  const auto retained_snapshot = make_retained_snapshot_fixture();
  assert_true(matches_retained_snapshot_contract(retained_snapshot),
              "diagnostics fixture should freeze the retained snapshot summary, actor_ref, evidence refs, redaction profile, and exporter hint");
  assert_true(retained_snapshot.summary == kDiagnosticsRetainedSummary,
              "diagnostics fixture should keep the retained snapshot summary stable");
  assert_true(retained_snapshot.command.actor_ref == kDiagnosticsRetainedActorRef,
              "diagnostics fixture should keep the retained actor_ref redacted");
}

void test_diagnostics_fixture_freezes_query_and_local_export_requests() {
  using dasall::infra::diagnostics::ExportFormat;
  using dasall::infra::diagnostics::ExportTarget;
  using dasall::tests::fixtures::infra::make_local_export_request_fixture;
  using dasall::tests::fixtures::infra::make_retained_snapshot_fixture;
  using dasall::tests::fixtures::infra::make_snapshot_query_fixture;
  using dasall::tests::support::assert_true;

  const auto retained_snapshot = make_retained_snapshot_fixture();
  const auto query = make_snapshot_query_fixture(retained_snapshot.snapshot_id);
  const auto export_request = make_local_export_request_fixture(retained_snapshot.snapshot_id);

  assert_true(query.is_valid(),
              "diagnostics fixture should produce a valid snapshot query for retained snapshot round trips");
  assert_true(export_request.is_valid(),
              "diagnostics fixture should produce a valid local JSON export request for retained snapshots");
  assert_true(export_request.snapshot_id == retained_snapshot.snapshot_id,
              "diagnostics fixture should keep snapshot query and export requests pinned to the same retained snapshot id");
  assert_true(export_request.target == ExportTarget::LocalFile &&
                  export_request.format == ExportFormat::Json,
              "diagnostics fixture should freeze local JSON as the retained snapshot export baseline");
}

}  // namespace

int main() {
  try {
    test_diagnostics_fixture_freezes_retained_snapshot_surface();
    test_diagnostics_fixture_freezes_query_and_local_export_requests();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}