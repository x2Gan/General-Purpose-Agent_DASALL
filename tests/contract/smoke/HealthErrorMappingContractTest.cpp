#include <array>
#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "health/HealthErrors.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

struct HealthErrorMappingExpectation {
  dasall::infra::HealthErrorCode code;
  std::string_view name;
  dasall::contracts::ResultCode result_code;
  std::string_view source_anchor;
};

void test_health_error_mapping_matrix_stays_frozen() {
  using dasall::contracts::ResultCode;
  using dasall::infra::HealthErrorCode;
  using dasall::infra::HealthErrorMapping;
  using dasall::infra::health_error_code_name;
  using dasall::infra::map_health_error_code;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(HealthErrorMapping{}.result_code), ResultCode>);

  constexpr std::array<HealthErrorMappingExpectation, 5> kFrozenMappings{ {
      {HealthErrorCode::ProbeTimeout,
       "INF_E_HEALTH_PROBE_TIMEOUT",
       ResultCode::ProviderTimeout,
       "6.8 probe timeout"},
      {HealthErrorCode::ProbeException,
       "INF_E_HEALTH_PROBE_EXCEPTION",
       ResultCode::ToolExecutionFailed,
       "6.8 probe exception"},
      {HealthErrorCode::ProbeNotFound,
       "INF_E_HEALTH_PROBE_NOT_FOUND",
       ResultCode::ValidationFieldMissing,
       "6.6 probe lookup"},
      {HealthErrorCode::PolicyInvalid,
       "INF_E_HEALTH_POLICY_INVALID",
       ResultCode::PolicyDenied,
       "6.8 policy invalid"},
      {HealthErrorCode::EventPublishFail,
       "INF_E_HEALTH_EVENT_PUBLISH_FAIL",
       ResultCode::ToolExecutionFailed,
       "6.8 event publish failure"},
  } };

  for (const auto& expectation : kFrozenMappings) {
    const auto mapping = map_health_error_code(expectation.code);
    assert_equal(static_cast<int>(expectation.result_code),
                 static_cast<int>(mapping.result_code),
                 std::string("health error mapping should remain frozen for ") +
                     std::string(expectation.name));
    assert_equal(std::string(expectation.name),
                 std::string(health_error_code_name(expectation.code)),
                 std::string("health error code name should remain stable for ") +
                     std::string(expectation.name));
    assert_equal(std::string(expectation.source_anchor),
                 std::string(mapping.source_anchor),
                 std::string("health error source anchor should remain frozen for ") +
                     std::string(expectation.name));
    assert_true(!mapping.reason.empty(),
                "each health private error mapping should carry a non-empty reason");
  }
}

void test_health_error_names_stay_local_to_health_boundary() {
  using dasall::infra::HealthErrorCode;
  using dasall::infra::health_error_code_name;
  using dasall::tests::support::assert_true;

  constexpr std::array<HealthErrorCode, 5> kFrozenCodes{
      HealthErrorCode::ProbeTimeout,
      HealthErrorCode::ProbeException,
      HealthErrorCode::ProbeNotFound,
      HealthErrorCode::PolicyInvalid,
      HealthErrorCode::EventPublishFail,
  };

  for (const auto code : kFrozenCodes) {
    assert_true(health_error_code_name(code).starts_with("INF_E_HEALTH_"),
                "health private error names should remain inside the INF_E_HEALTH_* local namespace");
  }
}

}  // namespace

int main() {
  try {
    test_health_error_mapping_matrix_stays_frozen();
    test_health_error_names_stay_local_to_health_boundary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}