#include <array>
#include <exception>
#include <iostream>
#include <string>

#include "health/HealthErrors.h"
#include "support/TestAssertions.h"

namespace {

struct HealthErrorExpectation {
  dasall::infra::HealthErrorCode code;
  int raw_value;
  std::string_view name;
};

void test_health_error_code_values_and_names_are_stable() {
  using dasall::infra::HealthErrorCode;
  using dasall::infra::health_error_code_name;
  using dasall::tests::support::assert_equal;

  constexpr std::array<HealthErrorExpectation, 5> kFrozenNames{ {
      {HealthErrorCode::ProbeTimeout, 1, "INF_E_HEALTH_PROBE_TIMEOUT"},
      {HealthErrorCode::ProbeException, 2, "INF_E_HEALTH_PROBE_EXCEPTION"},
      {HealthErrorCode::ProbeNotFound, 3, "INF_E_HEALTH_PROBE_NOT_FOUND"},
      {HealthErrorCode::PolicyInvalid, 4, "INF_E_HEALTH_POLICY_INVALID"},
      {HealthErrorCode::EventPublishFail, 5, "INF_E_HEALTH_EVENT_PUBLISH_FAIL"},
  } };

  for (const auto& expectation : kFrozenNames) {
    assert_equal(expectation.raw_value,
                 static_cast<int>(expectation.code),
                 "health error enum numeric values should remain stable");
    assert_equal(std::string(expectation.name),
                 std::string(health_error_code_name(expectation.code)),
                 "health error code names should remain stable");
  }
}

void test_health_error_mappings_keep_source_anchors_observable() {
  using dasall::infra::HealthErrorCode;
  using dasall::infra::health_error_code_name;
  using dasall::infra::map_health_error_code;
  using dasall::tests::support::assert_true;

  constexpr std::array<HealthErrorCode, 5> kFrozenCodes{
      HealthErrorCode::ProbeTimeout,
      HealthErrorCode::ProbeException,
      HealthErrorCode::ProbeNotFound,
      HealthErrorCode::PolicyInvalid,
      HealthErrorCode::EventPublishFail,
  };

  for (const auto code : kFrozenCodes) {
    const auto mapping = map_health_error_code(code);
    assert_true(health_error_code_name(code).starts_with("INF_E_HEALTH_"),
                "health private error names should remain inside the INF_E_HEALTH_* local namespace");
    assert_true(!mapping.source_anchor.empty(),
                "each health error mapping should carry a non-empty design source anchor");
    assert_true(!mapping.reason.empty(),
                "each health error mapping should carry a non-empty observable reason");
  }
}

}  // namespace

int main() {
  try {
    test_health_error_code_values_and_names_are_stable();
    test_health_error_mappings_keep_source_anchors_observable();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}