#include <array>
#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "../../../infra/include/config/ConfigErrors.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

struct ConfigErrorMappingExpectation {
  dasall::infra::config::ConfigErrorCode code;
  std::string_view name;
  dasall::contracts::ResultCode result_code;
};

void test_config_error_mapping_matrix_stays_frozen() {
  using dasall::contracts::ResultCode;
  using dasall::infra::config::ConfigErrorCode;
  using dasall::infra::config::ConfigErrorMapping;
  using dasall::infra::config::map_config_error_code;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(ConfigErrorMapping{}.result_code), ResultCode>);

  constexpr std::array<ConfigErrorMappingExpectation, 8> kFrozenMappings{{
      {ConfigErrorCode::NotFound, "INF_CFG_E_NOT_FOUND", ResultCode::ValidationFieldMissing},
      {ConfigErrorCode::TypeMismatch,
       "INF_CFG_E_TYPE_MISMATCH",
       ResultCode::ValidationFieldMissing},
      {ConfigErrorCode::InvalidSchema,
       "INF_CFG_E_INVALID_SCHEMA",
       ResultCode::ValidationFieldMissing},
      {ConfigErrorCode::Conflict, "INF_CFG_E_CONFLICT", ResultCode::ValidationFieldMissing},
      {ConfigErrorCode::SourceUnavailable,
       "INF_CFG_E_SOURCE_UNAVAILABLE",
       ResultCode::ProviderTimeout},
      {ConfigErrorCode::SecretResolveFail,
       "INF_CFG_E_SECRET_RESOLVE_FAIL",
       ResultCode::ProviderTimeout},
      {ConfigErrorCode::ApplyRejected, "INF_CFG_E_APPLY_REJECTED", ResultCode::PolicyDenied},
      {ConfigErrorCode::RollbackFailed,
       "INF_CFG_E_ROLLBACK_FAILED",
       ResultCode::RuntimeRetryExhausted},
  }};

  for (const auto& expectation : kFrozenMappings) {
    const auto mapping = map_config_error_code(expectation.code);
    assert_equal(static_cast<int>(expectation.result_code),
                 static_cast<int>(mapping.result_code),
                 std::string("config error mapping should remain frozen for ") +
                     std::string(expectation.name));
    assert_true(!mapping.reason.empty(),
                "each frozen config error mapping should carry a non-empty reason");
  }
}

void test_config_error_code_names_stay_private_to_config_boundary() {
  using dasall::infra::config::ConfigErrorCode;
  using dasall::infra::config::config_error_code_name;
  using dasall::tests::support::assert_true;

  constexpr std::array<ConfigErrorCode, 8> kFrozenCodes{
      ConfigErrorCode::NotFound,
      ConfigErrorCode::TypeMismatch,
      ConfigErrorCode::InvalidSchema,
      ConfigErrorCode::Conflict,
      ConfigErrorCode::SourceUnavailable,
      ConfigErrorCode::SecretResolveFail,
      ConfigErrorCode::ApplyRejected,
      ConfigErrorCode::RollbackFailed,
  };

  for (const auto code : kFrozenCodes) {
    const auto name = config_error_code_name(code);
    assert_true(name.starts_with("INF_CFG_E_"),
                "config private error names should remain inside INF_CFG_E_* local namespace");
  }
}

}  // namespace

int main() {
  try {
    test_config_error_mapping_matrix_stays_frozen();
    test_config_error_code_names_stay_private_to_config_boundary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}