#include <array>
#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "../../../infra/include/secret/SecretErrors.h"
#include "support/TestAssertions.h"

namespace {

struct SecretErrorMappingExpectation {
  dasall::infra::secret::SecretErrorCode code;
  std::string_view name;
  dasall::contracts::ResultCode result_code;
  std::string_view source_anchor;
};

void test_secret_error_mapping_matrix_stays_frozen() {
  using dasall::contracts::ResultCode;
  using dasall::infra::secret::SecretErrorCode;
  using dasall::infra::secret::SecretErrorMapping;
  using dasall::infra::secret::map_secret_error_code;
  using dasall::infra::secret::secret_error_code_name;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(SecretErrorMapping{}.result_code), ResultCode>);

  constexpr std::array<SecretErrorMappingExpectation, 9> kFrozenMappings{ {
      {SecretErrorCode::NotFound,
       "INF_E_SECRET_NOT_FOUND",
       ResultCode::ValidationFieldMissing,
       "6.6 get_secret"},
      {SecretErrorCode::AccessDenied,
       "INF_E_SECRET_ACCESS_DENIED",
       ResultCode::PolicyDenied,
       "6.8 permission failure"},
      {SecretErrorCode::BackendUnavailable,
       "INF_E_SECRET_BACKEND_UNAVAILABLE",
       ResultCode::ProviderTimeout,
       "6.8 backend failure"},
      {SecretErrorCode::LeaseExpired,
       "INF_E_SECRET_LEASE_EXPIRED",
       ResultCode::RuntimeRetryExhausted,
       "6.8 lifecycle failure"},
      {SecretErrorCode::VersionStale,
       "INF_E_SECRET_VERSION_STALE",
       ResultCode::RuntimeRetryExhausted,
       "6.8 stale handle"},
      {SecretErrorCode::MaterializeFailed,
       "INF_E_SECRET_MATERIALIZE_FAILED",
       ResultCode::ToolExecutionFailed,
       "6.6 materialize"},
      {SecretErrorCode::RotationValidationFailed,
       "INF_E_SECRET_ROTATION_VALIDATION_FAILED",
       ResultCode::ToolExecutionFailed,
       "6.8 rotation validation"},
      {SecretErrorCode::RotationRollbackFailed,
       "INF_E_SECRET_ROTATION_ROLLBACK_FAILED",
       ResultCode::RuntimeRetryExhausted,
       "6.8 rotation rollback"},
      {SecretErrorCode::AuditWriteFail,
       "INF_E_SECRET_AUDIT_WRITE_FAIL",
       ResultCode::ToolExecutionFailed,
       "6.8 audit failure"},
  } };

  for (const auto& expectation : kFrozenMappings) {
    const auto mapping = map_secret_error_code(expectation.code);
    assert_equal(static_cast<int>(expectation.result_code),
                 static_cast<int>(mapping.result_code),
                 std::string("secret error mapping should remain frozen for ") +
                     std::string(expectation.name));
    assert_equal(std::string(expectation.name),
                 std::string(secret_error_code_name(expectation.code)),
                 std::string("secret error code name should remain stable for ") +
                     std::string(expectation.name));
    assert_equal(std::string(expectation.source_anchor),
                 std::string(mapping.source_anchor),
                 std::string("secret error source anchor should remain frozen for ") +
                     std::string(expectation.name));
    assert_true(!mapping.reason.empty(),
                "each secret private error mapping should carry a non-empty reason");
  }
}

void test_secret_error_names_stay_local_to_secret_boundary() {
  using dasall::infra::secret::SecretErrorCode;
  using dasall::infra::secret::secret_error_code_name;
  using dasall::tests::support::assert_true;

  constexpr std::array<SecretErrorCode, 9> kFrozenCodes{
      SecretErrorCode::NotFound,
      SecretErrorCode::AccessDenied,
      SecretErrorCode::BackendUnavailable,
      SecretErrorCode::LeaseExpired,
      SecretErrorCode::VersionStale,
      SecretErrorCode::MaterializeFailed,
      SecretErrorCode::RotationValidationFailed,
      SecretErrorCode::RotationRollbackFailed,
      SecretErrorCode::AuditWriteFail,
  };

  for (const auto code : kFrozenCodes) {
    assert_true(secret_error_code_name(code).starts_with("INF_E_SECRET_"),
                "secret private error names should remain inside the INF_E_SECRET_* local namespace");
  }
}

}  // namespace

int main() {
  try {
    test_secret_error_mapping_matrix_stays_frozen();
    test_secret_error_names_stay_local_to_secret_boundary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}