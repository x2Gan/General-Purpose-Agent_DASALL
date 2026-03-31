#include <array>
#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "config/ConfigErrors.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

struct ConfigErrorNameExpectation {
  dasall::infra::config::ConfigErrorCode code;
  std::string_view name;
};

void test_config_error_code_names_are_stable() {
  using dasall::infra::config::ConfigErrorCode;
  using dasall::infra::config::config_error_code_name;
  using dasall::tests::support::assert_equal;

  constexpr std::array<ConfigErrorNameExpectation, 8> kFrozenNames{{
      {ConfigErrorCode::NotFound, "INF_CFG_E_NOT_FOUND"},
      {ConfigErrorCode::TypeMismatch, "INF_CFG_E_TYPE_MISMATCH"},
      {ConfigErrorCode::InvalidSchema, "INF_CFG_E_INVALID_SCHEMA"},
      {ConfigErrorCode::Conflict, "INF_CFG_E_CONFLICT"},
      {ConfigErrorCode::SourceUnavailable, "INF_CFG_E_SOURCE_UNAVAILABLE"},
      {ConfigErrorCode::SecretResolveFail, "INF_CFG_E_SECRET_RESOLVE_FAIL"},
      {ConfigErrorCode::ApplyRejected, "INF_CFG_E_APPLY_REJECTED"},
      {ConfigErrorCode::RollbackFailed, "INF_CFG_E_ROLLBACK_FAILED"},
  }};

  for (const auto& expectation : kFrozenNames) {
    assert_equal(std::string(expectation.name),
                 std::string(config_error_code_name(expectation.code)),
                 "config error code name should remain stable");
  }
}

void test_config_error_code_mapping_covers_all_frozen_codes() {
  using dasall::contracts::classify_result_code;
  using dasall::contracts::ResultCode;
  using dasall::contracts::ResultCodeCategory;
  using dasall::infra::config::ConfigErrorCode;
  using dasall::infra::config::ConfigErrorMapping;
  using dasall::infra::config::map_config_error_code;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(ConfigErrorMapping{}.result_code), ResultCode>);

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
    const auto mapping = map_config_error_code(code);
    const auto category = classify_result_code(mapping.result_code);

    assert_true(category == ResultCodeCategory::Validation ||
                    category == ResultCodeCategory::Policy ||
                    category == ResultCodeCategory::Provider ||
                    category == ResultCodeCategory::Runtime,
                "config private error mappings should stay inside existing contracts categories");
    assert_true(!mapping.reason.empty(),
                "each frozen config error mapping should carry an observable reason");
  }
}

}  // namespace

int main() {
  try {
    test_config_error_code_names_are_stable();
    test_config_error_code_mapping_covers_all_frozen_codes();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}