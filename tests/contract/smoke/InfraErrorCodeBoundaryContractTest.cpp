#include <array>
#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "../../../infra/include/InfraErrorCode.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_infra_error_code_maps_only_to_existing_contract_result_codes() {
  using dasall::contracts::ResultCode;
  using dasall::infra::InfraErrorCode;
  using dasall::infra::InfraErrorMapping;
  using dasall::infra::map_infra_error_code;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(InfraErrorMapping{}.result_code), ResultCode>);

  constexpr std::array<InfraErrorCode, 7> kFrozenCodes{
      InfraErrorCode::ConfigInvalid,
      InfraErrorCode::SecretUnavailable,
      InfraErrorCode::LogQueueFull,
      InfraErrorCode::AuditWriteFail,
      InfraErrorCode::HealthProbeTimeout,
      InfraErrorCode::OTAVerifyFail,
      InfraErrorCode::OTARollbackFail,
  };

  for (const auto code : kFrozenCodes) {
    const auto mapping = map_infra_error_code(code);
    assert_true(mapping.result_code == ResultCode::ValidationFieldMissing ||
                    mapping.result_code == ResultCode::ProviderTimeout ||
                    mapping.result_code == ResultCode::RuntimeRetryExhausted,
                "infra private errors should map only to existing contracts result codes");
  }
}

void test_infra_error_code_names_stay_private_to_infra_boundary() {
  using dasall::infra::InfraErrorCode;
  using dasall::infra::infra_error_code_name;
  using dasall::tests::support::assert_true;

  constexpr std::array<InfraErrorCode, 7> kFrozenCodes{
      InfraErrorCode::ConfigInvalid,
      InfraErrorCode::SecretUnavailable,
      InfraErrorCode::LogQueueFull,
      InfraErrorCode::AuditWriteFail,
      InfraErrorCode::HealthProbeTimeout,
      InfraErrorCode::OTAVerifyFail,
      InfraErrorCode::OTARollbackFail,
  };

  for (const auto code : kFrozenCodes) {
    const auto name = infra_error_code_name(code);
    assert_true(name.starts_with("INF_E_"),
                "infra private error names should remain inside INF_E_* local namespace");
  }
}

}  // namespace

int main() {
  try {
    test_infra_error_code_maps_only_to_existing_contract_result_codes();
    test_infra_error_code_names_stay_private_to_infra_boundary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}