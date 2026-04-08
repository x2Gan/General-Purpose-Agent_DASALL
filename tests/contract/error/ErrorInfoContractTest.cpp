#include <exception>
#include <iostream>

#include "support/TestAssertions.h"
#include "error/ErrorInfoGuards.h"

namespace {

dasall::contracts::ErrorInfo make_valid_error_info_sample() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCodeCategory;

  return ErrorInfo{
      .failure_type = ResultCodeCategory::Provider,
      .retryable = true,
      .safe_to_replan = true,
      .details = {
          .code = 4001,
          .message = "provider timed out",
          .stage = "executor",
      },
      .source_ref = {
          .ref_type = "tool_call",
          .ref_id = "tc-1001",
      },
  };
}

void test_valid_error_info_passes_required_guard() {
  using dasall::contracts::validate_error_info_required_fields;
  using dasall::tests::support::assert_true;

  // Positive case: all five required top-level fields and minimal details/
  // source_ref keys are present with valid semantics.
  const auto error_info = make_valid_error_info_sample();
  const auto result = validate_error_info_required_fields(error_info);

  assert_true(result.ok, "valid ErrorInfo sample should pass required-field guard");
}

void test_missing_failure_type_is_rejected() {
  using dasall::contracts::validate_error_info_required_fields;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  // Negative case: required field failure_type missing.
  auto error_info = make_valid_error_info_sample();
  error_info.failure_type.reset();

  const auto result = validate_error_info_required_fields(error_info);

  assert_true(!result.ok, "missing failure_type should be rejected");
  assert_equal("failure_type is required",
               std::string(result.reason),
               "guard reason should pinpoint missing failure_type");
}

void test_invalid_source_ref_type_is_rejected() {
  using dasall::contracts::validate_error_info_required_fields;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  // Negative case: source_ref.ref_type must stay in the frozen minimal set.
  auto error_info = make_valid_error_info_sample();
  error_info.source_ref.ref_type = "unknown_source";

  const auto result = validate_error_info_required_fields(error_info);

  assert_true(!result.ok, "unsupported source_ref.ref_type should be rejected");
  assert_equal("source_ref.ref_type is not supported",
               std::string(result.reason),
               "guard reason should identify unsupported source_ref.ref_type");
}

}  // namespace

int main() {
  try {
    test_valid_error_info_passes_required_guard();
    test_missing_failure_type_is_rejected();
    test_invalid_source_ref_type_is_rejected();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
