#include <array>
#include <exception>
#include <iostream>
#include <string>

#include "secret/SecretErrors.h"
#include "support/TestAssertions.h"

namespace {

struct SecretErrorExpectation {
  dasall::infra::secret::SecretErrorCode code;
  int raw_value;
  std::string_view name;
};

void test_secret_error_code_values_and_names_are_stable() {
  using dasall::infra::secret::SecretErrorCode;
  using dasall::infra::secret::secret_error_code_name;
  using dasall::tests::support::assert_equal;

  constexpr std::array<SecretErrorExpectation, 9> kFrozenNames{ {
      {SecretErrorCode::NotFound, 1, "INF_E_SECRET_NOT_FOUND"},
      {SecretErrorCode::AccessDenied, 2, "INF_E_SECRET_ACCESS_DENIED"},
      {SecretErrorCode::BackendUnavailable, 3, "INF_E_SECRET_BACKEND_UNAVAILABLE"},
      {SecretErrorCode::LeaseExpired, 4, "INF_E_SECRET_LEASE_EXPIRED"},
      {SecretErrorCode::VersionStale, 5, "INF_E_SECRET_VERSION_STALE"},
      {SecretErrorCode::MaterializeFailed, 6, "INF_E_SECRET_MATERIALIZE_FAILED"},
      {SecretErrorCode::RotationValidationFailed, 7, "INF_E_SECRET_ROTATION_VALIDATION_FAILED"},
      {SecretErrorCode::RotationRollbackFailed, 8, "INF_E_SECRET_ROTATION_ROLLBACK_FAILED"},
      {SecretErrorCode::AuditWriteFail, 9, "INF_E_SECRET_AUDIT_WRITE_FAIL"},
  } };

  for (const auto& expectation : kFrozenNames) {
    assert_equal(expectation.raw_value,
                 static_cast<int>(expectation.code),
                 "secret error enum numeric values should remain stable");
    assert_equal(std::string(expectation.name),
                 std::string(secret_error_code_name(expectation.code)),
                 "secret error code names should remain stable");
  }
}

void test_secret_error_mappings_keep_source_anchors_observable() {
  using dasall::infra::secret::SecretErrorCode;
  using dasall::infra::secret::map_secret_error_code;
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
    const auto mapping = map_secret_error_code(code);
    assert_true(secret_error_code_name(code).starts_with("INF_E_SECRET_"),
                "secret private error names should remain inside the INF_E_SECRET_* local namespace");
    assert_true(!mapping.source_anchor.empty(),
                "each secret error mapping should carry a non-empty design source anchor");
    assert_true(!mapping.reason.empty(),
                "each secret error mapping should carry a non-empty observable reason");
  }
}

}  // namespace

int main() {
  try {
    test_secret_error_code_values_and_names_are_stable();
    test_secret_error_mappings_keep_source_anchors_observable();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}