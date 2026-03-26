#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

#include "../../../infra/include/IInfrastructureService.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_infrastructure_service_result_uses_contract_error_types_only() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::InfraOperationResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(InfraOperationResult{}.result_code), ResultCode>);
  static_assert(
      std::is_same_v<decltype(InfraOperationResult{}.error), std::optional<ErrorInfo>>);

  const auto failure = InfraOperationResult::failure(ResultCode::ValidationFieldMissing,
                                                     "command is required",
                                                     "infra.execute",
                                                     "InfraServiceFacade");

  assert_true(!failure.ok,
              "boundary failure results should remain explicit failures");
  assert_true(failure.references_only_contract_error_types(),
              "infrastructure service should expose only contracts ResultCode/ErrorInfo types");
}

void test_infrastructure_service_command_request_stays_minimal_until_signature_freezes() {
  using dasall::infra::InfraCommandRequest;
  using dasall::infra::InfrastructureConfig;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(InfraCommandRequest{}.name), std::string>);
  static_assert(std::is_same_v<decltype(InfrastructureConfig{}.profile), std::string>);

  const InfraCommandRequest request{.name = "ota.precheck"};
  assert_true(request.is_valid(),
              "command skeleton should freeze only the command name until payload details are designed");
}

}  // namespace

int main() {
  try {
    test_infrastructure_service_result_uses_contract_error_types_only();
    test_infrastructure_service_command_request_stays_minimal_until_signature_freezes();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}