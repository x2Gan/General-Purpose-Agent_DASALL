#include <exception>
#include <iostream>
#include <string>

#include "DiagnosticsSnapshotFixture.h"
#include "diagnostics/DiagnosticsServiceFacade.h"
#include "support/TestAssertions.h"

namespace {

void test_diagnostics_snapshot_store_contract_round_trips_retained_snapshot() {
  using dasall::infra::diagnostics::DiagnosticsServiceFacade;
  using dasall::tests::fixtures::infra::make_execute_command_fixture;
  using dasall::tests::fixtures::infra::make_snapshot_query_fixture;
  using dasall::tests::fixtures::infra::matches_retained_snapshot_contract;
  using dasall::tests::support::assert_true;

  DiagnosticsServiceFacade service;
  assert_true(service.start(),
              "diagnostics snapshot store contract should start the facade before execute/get_snapshot");
  service.inject_snapshot_store_current_time_for_test("2026-04-07T21:00:00Z");

  const auto execute_result = service.execute(make_execute_command_fixture());
  assert_true(execute_result.ok,
              "diagnostics snapshot store contract should execute the frozen health.snapshot command successfully");
  assert_true(matches_retained_snapshot_contract(execute_result.snapshot),
              "diagnostics snapshot store contract should persist a retained snapshot with the frozen redaction surface");

  const auto get_result = service.get_snapshot(
      make_snapshot_query_fixture(execute_result.snapshot.snapshot_id));
  assert_true(get_result.ok,
              "diagnostics snapshot store contract should read back the retained snapshot by snapshot_id");
  assert_true(matches_retained_snapshot_contract(get_result.snapshot),
              "diagnostics snapshot store contract should keep retained snapshot shape stable across get_snapshot");
  assert_true(get_result.snapshot.snapshot_id == execute_result.snapshot.snapshot_id,
              "diagnostics snapshot store contract should keep snapshot_id stable across execute -> get_snapshot");
  assert_true(get_result.snapshot.summary == execute_result.snapshot.summary,
              "diagnostics snapshot store contract should keep summary stable across execute -> get_snapshot");
  assert_true(get_result.snapshot.command.actor_ref == execute_result.snapshot.command.actor_ref,
              "diagnostics snapshot store contract should keep the redacted actor_ref stable across execute -> get_snapshot");
  assert_true(get_result.snapshot.evidence_refs == execute_result.snapshot.evidence_refs,
              "diagnostics snapshot store contract should keep evidence refs stable across execute -> get_snapshot");
}

void test_diagnostics_snapshot_store_contract_rejects_unknown_snapshot_id() {
  using dasall::contracts::ResultCode;
  using dasall::infra::diagnostics::DiagnosticsServiceFacade;
  using dasall::tests::fixtures::infra::make_snapshot_query_fixture;
  using dasall::tests::support::assert_true;

  DiagnosticsServiceFacade service;
  assert_true(service.start(),
              "diagnostics snapshot store contract failure path should start the facade before get_snapshot");
  service.inject_snapshot_store_current_time_for_test("2026-04-07T21:00:00Z");

  const auto get_result =
      service.get_snapshot(make_snapshot_query_fixture("diag-retained-missing-011"));
  assert_true(!get_result.ok,
              "diagnostics snapshot store contract should reject snapshot ids that do not resolve to retained snapshots");
  assert_true(get_result.references_only_contract_error_types(),
              "diagnostics snapshot store contract should keep get_snapshot failures inside the contracts error boundary");
  assert_true(get_result.result_code == ResultCode::ValidationFieldMissing,
              "diagnostics snapshot store contract should map unknown snapshot ids to ValidationFieldMissing");
  assert_true(get_result.error.has_value() &&
                  get_result.error->details.stage == "diagnostics.get_snapshot",
              "diagnostics snapshot store contract should name diagnostics.get_snapshot as the failing seam");
}

}  // namespace

int main() {
  try {
    test_diagnostics_snapshot_store_contract_round_trips_retained_snapshot();
    test_diagnostics_snapshot_store_contract_rejects_unknown_snapshot_id();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}