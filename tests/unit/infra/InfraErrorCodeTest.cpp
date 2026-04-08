#include <array>
#include <exception>
#include <iostream>

#include "InfraErrorCode.h"
#include "support/TestAssertions.h"

namespace {

void test_infra_error_code_names_and_mapping_are_stable() {
  using dasall::infra::InfraErrorCode;
  using dasall::infra::infra_error_code_name;
  using dasall::infra::map_infra_error_code;
  using dasall::tests::support::assert_equal;

  assert_equal(std::string("INF_E_CONFIG_INVALID"),
               std::string(infra_error_code_name(InfraErrorCode::ConfigInvalid)),
               "config invalid code name should remain stable");
  assert_equal(std::string("INF_E_SECRET_UNAVAILABLE"),
               std::string(infra_error_code_name(InfraErrorCode::SecretUnavailable)),
               "secret unavailable code name should remain stable");
  assert_equal(std::string("INF_E_LOG_QUEUE_FULL"),
               std::string(infra_error_code_name(InfraErrorCode::LogQueueFull)),
               "log queue full code name should remain stable");
  assert_equal(std::string("INF_E_AUDIT_WRITE_FAIL"),
               std::string(infra_error_code_name(InfraErrorCode::AuditWriteFail)),
               "audit write fail code name should remain stable");
  assert_equal(std::string("INF_E_HEALTH_PROBE_TIMEOUT"),
               std::string(infra_error_code_name(InfraErrorCode::HealthProbeTimeout)),
               "health probe timeout code name should remain stable");
  assert_equal(std::string("INF_E_OTA_VERIFY_FAIL"),
               std::string(infra_error_code_name(InfraErrorCode::OTAVerifyFail)),
               "OTA verify fail code name should remain stable");
  assert_equal(std::string("INF_E_OTA_ROLLBACK_FAIL"),
               std::string(infra_error_code_name(InfraErrorCode::OTARollbackFail)),
               "OTA rollback fail code name should remain stable");
  assert_equal(std::string("INF_E_OTA_BOOT_CONFIRM_TIMEOUT"),
               std::string(infra_error_code_name(InfraErrorCode::OTABootConfirmTimeout)),
               "OTA boot confirm timeout code name should remain stable");

  assert_equal(static_cast<int>(dasall::contracts::ResultCode::ValidationFieldMissing),
               static_cast<int>(map_infra_error_code(InfraErrorCode::ConfigInvalid).result_code),
               "config invalid should map to contracts validation category");
  assert_equal(static_cast<int>(dasall::contracts::ResultCode::ProviderTimeout),
               static_cast<int>(map_infra_error_code(InfraErrorCode::SecretUnavailable).result_code),
               "secret unavailable should map to contracts provider category");
  assert_equal(static_cast<int>(dasall::contracts::ResultCode::RuntimeRetryExhausted),
               static_cast<int>(map_infra_error_code(InfraErrorCode::LogQueueFull).result_code),
               "log queue full should map to contracts runtime category");
}

void test_infra_error_code_mapping_covers_all_frozen_codes() {
  using dasall::contracts::classify_result_code;
  using dasall::contracts::ResultCodeCategory;
  using dasall::infra::InfraErrorCode;
  using dasall::infra::map_infra_error_code;
  using dasall::tests::support::assert_true;

    constexpr std::array<InfraErrorCode, 8> kFrozenCodes{
      InfraErrorCode::ConfigInvalid,
      InfraErrorCode::SecretUnavailable,
      InfraErrorCode::LogQueueFull,
      InfraErrorCode::AuditWriteFail,
      InfraErrorCode::HealthProbeTimeout,
      InfraErrorCode::OTAVerifyFail,
      InfraErrorCode::OTARollbackFail,
      InfraErrorCode::OTABootConfirmTimeout,
  };

  for (const auto code : kFrozenCodes) {
    const auto mapping = map_infra_error_code(code);
    const auto category = classify_result_code(mapping.result_code);

    assert_true(category == ResultCodeCategory::Validation ||
                    category == ResultCodeCategory::Provider ||
                    category == ResultCodeCategory::Runtime,
                "infra private error mapping should stay inside existing contracts categories");
    assert_true(!mapping.reason.empty(),
                "each frozen infra error mapping should carry an observable reason");
  }
}

}  // namespace

int main() {
  try {
    test_infra_error_code_names_and_mapping_are_stable();
    test_infra_error_code_mapping_covers_all_frozen_codes();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}