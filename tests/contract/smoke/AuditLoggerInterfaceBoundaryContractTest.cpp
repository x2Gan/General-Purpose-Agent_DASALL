#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

#include "../../../infra/include/audit/IAuditLogger.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_audit_logger_results_use_contract_error_types_only() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::audit::AuditExportResult;
  using dasall::infra::audit::AuditWriteResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(AuditWriteResult{}.result_code), ResultCode>);
  static_assert(std::is_same_v<decltype(AuditWriteResult{}.error), std::optional<ErrorInfo>>);
  static_assert(std::is_same_v<decltype(AuditExportResult{}.result_code), ResultCode>);
  static_assert(std::is_same_v<decltype(AuditExportResult{}.error), std::optional<ErrorInfo>>);

  const auto write_failure = AuditWriteResult::failure(ResultCode::ValidationFieldMissing,
                                                       "audit event is required",
                                                       "audit.write",
                                                       "IAuditLogger");
  const auto export_failure = AuditExportResult::failure(ResultCode::ValidationFieldMissing,
                                                         "export filter is required",
                                                         "audit.export",
                                                         "IAuditLogger");

  assert_true(!write_failure.ok,
              "audit write failures should remain explicit failures");
  assert_true(write_failure.references_only_contract_error_types(),
              "IAuditLogger write path should expose only contracts ResultCode/ErrorInfo types");
  assert_true(!export_failure.ok,
              "audit export failures should remain explicit failures");
  assert_true(export_failure.references_only_contract_error_types(),
              "IAuditLogger export path should expose only contracts ResultCode/ErrorInfo types");
}

void test_audit_logger_keeps_export_filter_as_local_placeholder_type() {
  using dasall::infra::AuditEvent;
  using dasall::infra::audit::AuditExportFilter;
  using dasall::infra::audit::AuditExportResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(AuditExportFilter{}.opaque_selector), std::string>);
  static_assert(std::is_same_v<decltype(AuditExportResult{}.records), std::vector<AuditEvent>>);

  const AuditExportFilter valid_filter{.opaque_selector = "actor=runtime"};
  const AuditExportFilter invalid_filter{};

  assert_true(valid_filter.is_specified(),
              "non-empty opaque filter should satisfy the placeholder export guard");
  assert_true(!invalid_filter.is_specified(),
              "empty opaque filter should remain invalid until export filter semantics are fully designed");
}

}  // namespace

int main() {
  try {
    test_audit_logger_results_use_contract_error_types_only();
    test_audit_logger_keeps_export_filter_as_local_placeholder_type();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}