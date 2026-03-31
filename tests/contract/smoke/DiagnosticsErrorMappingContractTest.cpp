#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

#include "../../../infra/include/diagnostics/DiagnosticsTypes.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_command_decision_reason_codes_map_only_to_existing_contract_result_codes() {
  using dasall::contracts::ResultCode;
  using dasall::infra::diagnostics::CommandDecision;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(CommandDecision{}.mapped_result_code()),
                               std::optional<ResultCode>>);

  const auto denied_mapping =
      CommandDecision::map_reason_code_to_result_code("diag_command_denied");
  assert_true(denied_mapping.has_value(),
              "diagnostics deny reason_code should stay mapped to an existing contracts result code");
  assert_equal(static_cast<int>(ResultCode::PolicyDenied),
               static_cast<int>(*denied_mapping),
               "diag_command_denied should remain in the contracts policy failure domain");

  const auto invalid_mapping =
      CommandDecision::map_reason_code_to_result_code("diag_command_invalid");
  assert_true(invalid_mapping.has_value(),
              "diagnostics invalid-command reason_code should stay mapped to an existing contracts result code");
  assert_equal(static_cast<int>(ResultCode::ValidationFieldMissing),
               static_cast<int>(*invalid_mapping),
               "diag_command_invalid should remain in the contracts validation failure domain");
}

void test_command_decision_reason_code_mapping_rejects_unknown_values() {
  using dasall::infra::diagnostics::CommandDecision;
  using dasall::tests::support::assert_true;

  const auto unknown_mapping = CommandDecision::map_reason_code_to_result_code("diag_unknown");
  assert_true(!unknown_mapping.has_value(),
              "diagnostics command decision should reject reason_code values outside the frozen contracts mapping table");
}

void test_snapshot_export_result_failure_stays_bound_to_existing_contract_error_types() {
  using dasall::contracts::ResultCode;
  using dasall::infra::diagnostics::SnapshotExportResult;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(SnapshotExportResult{}.result_code), ResultCode>);

  const auto failure = SnapshotExportResult::failure(ResultCode::ProviderTimeout,
                                                     "diagnostics export target is unavailable",
                                                     "diagnostics.export",
                                                     "DiagnosticsExportManager");
  assert_true(failure.is_valid(),
              "diagnostics export failure should remain valid when it carries an existing contracts result code and ErrorInfo payload");
  assert_true(failure.references_only_contract_error_types(),
              "diagnostics export failure should stay inside existing contracts ResultCode/ErrorInfo types");
  assert_equal(static_cast<int>(ResultCode::ProviderTimeout),
               static_cast<int>(failure.result_code),
               "diagnostics export failure should keep the bound contracts error code stable across the boundary");
}

}  // namespace

int main() {
  try {
    test_command_decision_reason_codes_map_only_to_existing_contract_result_codes();
    test_command_decision_reason_code_mapping_rejects_unknown_values();
    test_snapshot_export_result_failure_stays_bound_to_existing_contract_error_types();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}