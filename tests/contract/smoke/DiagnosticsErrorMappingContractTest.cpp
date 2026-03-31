#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

#include "../../../infra/include/diagnostics/DiagnosticsErrors.h"
#include "../../../infra/include/diagnostics/DiagnosticsTypes.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

struct DiagnosticsErrorMappingExpectation {
  dasall::infra::diagnostics::DiagnosticsErrorCode code;
  std::string_view name;
  dasall::contracts::ResultCode result_code;
};

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

void test_diagnostics_error_mapping_matrix_stays_frozen() {
  using dasall::contracts::ResultCode;
  using dasall::infra::diagnostics::DiagnosticsErrorCode;
  using dasall::infra::diagnostics::DiagnosticsErrorMapping;
  using dasall::infra::diagnostics::diagnostics_error_code_name;
  using dasall::infra::diagnostics::map_diagnostics_error_code;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(DiagnosticsErrorMapping{}.result_code), ResultCode>);

  constexpr std::array<DiagnosticsErrorMappingExpectation, 8> kFrozenMappings{{
      {DiagnosticsErrorCode::CommandDenied,
       "INF_E_DIAG_COMMAND_DENIED",
       ResultCode::PolicyDenied},
      {DiagnosticsErrorCode::CommandInvalid,
       "INF_E_DIAG_COMMAND_INVALID",
       ResultCode::ValidationFieldMissing},
      {DiagnosticsErrorCode::ExecTimeout,
       "INF_E_DIAG_EXEC_TIMEOUT",
       ResultCode::ProviderTimeout},
      {DiagnosticsErrorCode::ExecFail,
       "INF_E_DIAG_EXEC_FAIL",
       ResultCode::ToolExecutionFailed},
      {DiagnosticsErrorCode::RedactionFail,
       "INF_E_DIAG_REDACTION_FAIL",
       ResultCode::ToolExecutionFailed},
      {DiagnosticsErrorCode::SnapshotStoreFail,
       "INF_E_DIAG_SNAPSHOT_STORE_FAIL",
       ResultCode::RuntimeRetryExhausted},
      {DiagnosticsErrorCode::ExportFail,
       "INF_E_DIAG_EXPORT_FAIL",
       ResultCode::ProviderTimeout},
      {DiagnosticsErrorCode::RemoteExportDisabled,
       "INF_E_DIAG_REMOTE_EXPORT_DISABLED",
       ResultCode::PolicyDenied},
  }};

  for (const auto& expectation : kFrozenMappings) {
    const auto mapping = map_diagnostics_error_code(expectation.code);
    assert_equal(static_cast<int>(expectation.result_code),
                 static_cast<int>(mapping.result_code),
                 std::string("diagnostics error mapping should remain frozen for ") +
                     std::string(expectation.name));
    assert_equal(std::string(expectation.name),
                 std::string(diagnostics_error_code_name(expectation.code)),
                 std::string("diagnostics private error code name should remain stable for ") +
                     std::string(expectation.name));
    assert_true(!mapping.reason.empty(),
                "each diagnostics private error mapping should carry a non-empty reason");
  }
}

}  // namespace

int main() {
  try {
    test_command_decision_reason_codes_map_only_to_existing_contract_result_codes();
    test_command_decision_reason_code_mapping_rejects_unknown_values();
    test_snapshot_export_result_failure_stays_bound_to_existing_contract_error_types();
    test_diagnostics_error_mapping_matrix_stays_frozen();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}