#include <exception>
#include <iostream>
#include <string>

#include "PlatformError.h"
#include "PlatformResult.h"
#include "support/TestAssertions.h"

namespace {

void test_platform_error_category_mapping_remains_in_frozen_contract_domains() {
  using dasall::contracts::ResultCodeCategory;
  using dasall::platform::map_platform_error_category_to_contracts;
  using dasall::platform::PlatformErrorCategory;
  using dasall::tests::support::assert_true;

  assert_true(map_platform_error_category_to_contracts(PlatformErrorCategory::Validation) ==
                  ResultCodeCategory::Validation,
              "validation errors should map to contracts validation category");
  assert_true(map_platform_error_category_to_contracts(PlatformErrorCategory::Resource) ==
                  ResultCodeCategory::Runtime,
              "resource errors should map to contracts runtime category");
  assert_true(map_platform_error_category_to_contracts(PlatformErrorCategory::IO) ==
                  ResultCodeCategory::Provider,
              "io errors should map to contracts provider category");
  assert_true(map_platform_error_category_to_contracts(PlatformErrorCategory::Network) ==
                  ResultCodeCategory::Provider,
              "network errors should map to contracts provider category");
  assert_true(map_platform_error_category_to_contracts(PlatformErrorCategory::IPC) ==
                  ResultCodeCategory::Provider,
              "ipc errors should map to contracts provider category");
  assert_true(map_platform_error_category_to_contracts(PlatformErrorCategory::Internal) ==
                  ResultCodeCategory::Runtime,
              "internal errors should map to contracts runtime category");
}

void test_platform_error_requires_observable_detail_and_consistent_errno_context() {
  using dasall::platform::PlatformError;
  using dasall::platform::PlatformErrorCategory;
  using dasall::platform::PlatformErrorCode;
  using dasall::tests::support::assert_true;

  const PlatformError valid_error{
      .code = PlatformErrorCode::Timeout,
      .category = PlatformErrorCategory::Network,
      .retryable_hint = true,
      .syscall_name = "connect",
      .errno_value = 110,
      .detail = "connect timeout reached",
  };

  const PlatformError missing_detail{
      .code = PlatformErrorCode::InternalFailure,
      .category = PlatformErrorCategory::Internal,
      .retryable_hint = false,
      .syscall_name = "poll",
      .errno_value = 5,
      .detail = {},
  };

  const PlatformError errno_without_syscall{
      .code = PlatformErrorCode::PermissionDenied,
      .category = PlatformErrorCategory::IO,
      .retryable_hint = false,
      .syscall_name = {},
      .errno_value = 13,
      .detail = "permission denied",
  };

  assert_true(valid_error.has_consistent_values(),
              "platform error with detail and syscall context should be valid");
  assert_true(!missing_detail.has_consistent_values(),
              "platform error should require non-empty detail for observability");
  assert_true(!errno_without_syscall.has_consistent_values(),
              "platform error should reject errno values without syscall name");
}

void test_platform_result_keeps_success_failure_exclusive() {
  using dasall::platform::PlatformError;
  using dasall::platform::PlatformErrorCategory;
  using dasall::platform::PlatformErrorCode;
  using dasall::platform::PlatformResult;
  using dasall::tests::support::assert_true;

  const auto success_result = PlatformResult<int>::success(7);
  const auto failure_result = PlatformResult<int>::failure(PlatformError{
      .code = PlatformErrorCode::Timeout,
      .category = PlatformErrorCategory::Network,
      .retryable_hint = true,
      .syscall_name = "recv",
      .errno_value = 11,
      .detail = "recv timeout",
  });

  PlatformResult<int> invalid_result;
  invalid_result.value = 1;
  invalid_result.error = PlatformError{
      .code = PlatformErrorCode::InternalFailure,
      .category = PlatformErrorCategory::Internal,
      .retryable_hint = false,
      .syscall_name = "",
      .errno_value = std::nullopt,
      .detail = "unexpected mixed state",
  };

  assert_true(success_result.ok(), "success result should report ok");
  assert_true(success_result.has_consistent_values(),
              "success result should keep value/error exclusivity");
  assert_true(!failure_result.ok(), "failure result should report not ok");
  assert_true(failure_result.has_consistent_values(),
              "failure result should keep value/error exclusivity");
  assert_true(!invalid_result.has_consistent_values(),
              "platform result should reject mixed value+error state");
}

}  // namespace

int main() {
  try {
    test_platform_error_category_mapping_remains_in_frozen_contract_domains();
    test_platform_error_requires_observable_detail_and_consistent_errno_context();
    test_platform_result_keeps_success_failure_exclusive();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}