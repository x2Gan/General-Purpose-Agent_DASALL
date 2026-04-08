#include <exception>
#include <iostream>

#include "support/TestAssertions.h"
#include "error/ResultCode.h"

namespace {

void test_five_primary_result_code_categories_are_stable() {
  using dasall::contracts::ResultCode;
  using dasall::contracts::ResultCodeCategory;
  using dasall::contracts::classify_result_code;
  using dasall::tests::support::assert_equal;

  // Positive cases: each frozen example code must map to its intended
  // first-level category without ambiguity.
  assert_equal(static_cast<int>(ResultCodeCategory::Validation),
               static_cast<int>(classify_result_code(ResultCode::ValidationFieldMissing)),
               "validation sample code should map to validation category");
  assert_equal(static_cast<int>(ResultCodeCategory::Policy),
               static_cast<int>(classify_result_code(ResultCode::PolicyDenied)),
               "policy sample code should map to policy category");
  assert_equal(static_cast<int>(ResultCodeCategory::Tool),
               static_cast<int>(classify_result_code(ResultCode::ToolExecutionFailed)),
               "tool sample code should map to tool category");
  assert_equal(static_cast<int>(ResultCodeCategory::Provider),
               static_cast<int>(classify_result_code(ResultCode::ProviderTimeout)),
               "provider sample code should map to provider category");
  assert_equal(static_cast<int>(ResultCodeCategory::Runtime),
               static_cast<int>(classify_result_code(ResultCode::RuntimeRetryExhausted)),
               "runtime sample code should map to runtime category");
}

void test_tool_provider_segment_boundary_is_consistent() {
  using dasall::contracts::ResultCodeCategory;
  using dasall::contracts::classify_result_code_segment;
  using dasall::tests::support::assert_equal;

  // Boundary check: 3999 remains tool and 4000 enters provider.
  assert_equal(static_cast<int>(ResultCodeCategory::Tool),
               static_cast<int>(classify_result_code_segment(3999)),
               "upper tool boundary should remain tool");
  assert_equal(static_cast<int>(ResultCodeCategory::Provider),
               static_cast<int>(classify_result_code_segment(4000)),
               "lower provider boundary should map to provider");
}

void test_out_of_range_result_code_is_rejected() {
  using dasall::contracts::ResultCodeCategory;
  using dasall::contracts::classify_result_code_value;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  // Negative case: 7000 is outside the frozen five-domain ranges and must be
  // treated as unknown instead of being silently mapped.
  const auto classification = classify_result_code_value(7000);

  assert_true(!classification.ok, "out-of-range result code should not be accepted as valid");
  assert_equal(static_cast<int>(ResultCodeCategory::Unknown),
               static_cast<int>(classification.category),
               "out-of-range result code should be classified as unknown");
}

}  // namespace

int main() {
  try {
    test_five_primary_result_code_categories_are_stable();
    test_tool_provider_segment_boundary_is_consistent();
    test_out_of_range_result_code_is_rejected();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
