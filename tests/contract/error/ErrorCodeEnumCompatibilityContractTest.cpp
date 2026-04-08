#include <array>
#include <exception>
#include <iostream>
#include <string>

#include "boundary/EnumLifecycleGuards.h"
#include "support/TestAssertions.h"
#include "error/ResultCode.h"

namespace {

using dasall::contracts::EnumLifecycleDescriptor;
using dasall::contracts::ResultCode;
using dasall::contracts::ResultCodeCategory;
using dasall::contracts::classify_result_code_segment;
using dasall::contracts::classify_result_code_value;
using dasall::contracts::normalize_enum_with_lifecycle;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

// Builds a stable enum lifecycle descriptor used by compatibility tests.
// The descriptor keeps 0 as Unspecified sentinel and marks 3 as deprecated.
EnumLifecycleDescriptor make_enum_descriptor_with_deprecated_tail() {
  static constexpr std::array<int, 4> kKnownValues{0, 1, 2, 3};
  static constexpr std::array<int, 1> kDeprecatedValues{3};

  return EnumLifecycleDescriptor{
      .known_values = kKnownValues.data(),
      .known_value_count = kKnownValues.size(),
      .unspecified_value = 0,
      .deprecated_values = kDeprecatedValues.data(),
      .deprecated_value_count = kDeprecatedValues.size(),
  };
}

// Positive coverage: frozen ResultCode numeric values should remain stable.
// Any accidental renumbering is treated as a compatibility-breaking change.
void test_result_code_numeric_snapshot_is_stable() {
  assert_equal(1001,
               static_cast<int>(ResultCode::ValidationFieldMissing),
               "ValidationFieldMissing numeric value must remain stable");
  assert_equal(2001,
               static_cast<int>(ResultCode::PolicyDenied),
               "PolicyDenied numeric value must remain stable");
  assert_equal(3001,
               static_cast<int>(ResultCode::ToolExecutionFailed),
               "ToolExecutionFailed numeric value must remain stable");
  assert_equal(4001,
               static_cast<int>(ResultCode::ProviderTimeout),
               "ProviderTimeout numeric value must remain stable");
  assert_equal(5001,
               static_cast<int>(ResultCode::RuntimeRetryExhausted),
               "RuntimeRetryExhausted numeric value must remain stable");
}

// Positive coverage: category boundaries defined by WP02-T004 must keep
// deterministic segment mapping at all range edges.
void test_result_code_segment_boundaries_are_stable() {
  assert_equal(static_cast<int>(ResultCodeCategory::Validation),
               static_cast<int>(classify_result_code_segment(1000)),
               "1000 should stay in validation segment");
  assert_equal(static_cast<int>(ResultCodeCategory::Validation),
               static_cast<int>(classify_result_code_segment(1999)),
               "1999 should stay in validation segment");
  assert_equal(static_cast<int>(ResultCodeCategory::Policy),
               static_cast<int>(classify_result_code_segment(2000)),
               "2000 should map to policy segment");
  assert_equal(static_cast<int>(ResultCodeCategory::Tool),
               static_cast<int>(classify_result_code_segment(3000)),
               "3000 should map to tool segment");
  assert_equal(static_cast<int>(ResultCodeCategory::Provider),
               static_cast<int>(classify_result_code_segment(4000)),
               "4000 should map to provider segment");
  assert_equal(static_cast<int>(ResultCodeCategory::Runtime),
               static_cast<int>(classify_result_code_segment(5000)),
               "5000 should map to runtime segment");
}

// Negative coverage: values outside frozen ranges must not be silently accepted.
void test_out_of_range_result_code_is_rejected() {
  const auto result = classify_result_code_value(9000);

  assert_true(!result.ok,
              "out-of-range result code should be rejected");
  assert_equal(static_cast<int>(ResultCodeCategory::Unknown),
               static_cast<int>(result.category),
               "out-of-range result code should classify as unknown");
}

// Positive compatibility coverage: unknown enum values should degrade to
// Unspecified sentinel instead of causing parser hard failure.
void test_unknown_enum_value_downgrades_to_unspecified() {
  const auto descriptor = make_enum_descriptor_with_deprecated_tail();
  const auto result = normalize_enum_with_lifecycle(77, descriptor);

  assert_true(result.ok,
              "unknown enum normalization should succeed");
  assert_equal(0,
               result.normalized_value,
               "unknown enum value should downgrade to Unspecified sentinel");
  assert_true(result.downgraded_from_unknown,
              "unknown downgrade path should be marked for compatibility evidence");
}

// Positive compatibility coverage: deprecated values must remain readable in the
// transition window, but should carry a deprecated usage marker.
void test_deprecated_enum_value_is_preserved_with_marker() {
  const auto descriptor = make_enum_descriptor_with_deprecated_tail();
  const auto result = normalize_enum_with_lifecycle(3, descriptor);

  assert_true(result.ok,
              "deprecated enum value should still be parseable");
  assert_equal(3,
               result.normalized_value,
               "deprecated enum value should remain unchanged");
  assert_true(result.deprecated_value_used,
              "deprecated enum path should be marked");
}

// Negative compatibility coverage: deleting Unspecified sentinel from known
// values is a breaking change and must be blocked by lifecycle guard.
void test_missing_unspecified_sentinel_is_rejected() {
  static constexpr std::array<int, 3> kBrokenKnownValues{1, 2, 3};
  const EnumLifecycleDescriptor broken_descriptor{
      .known_values = kBrokenKnownValues.data(),
      .known_value_count = kBrokenKnownValues.size(),
      .unspecified_value = 0,
      .deprecated_values = nullptr,
      .deprecated_value_count = 0,
  };

  const auto result = normalize_enum_with_lifecycle(2, broken_descriptor);

  assert_true(!result.ok,
              "descriptor without Unspecified sentinel should fail");
  assert_equal(std::string("Unspecified sentinel must be present in known_values"),
               std::string(result.reason),
               "missing Unspecified sentinel should report stable reason");
}

}  // namespace

int main() {
  int passed = 0;
  int failed = 0;

  // Shared runner keeps output format aligned with other contract tests.
  auto run_test = [&](const char* name, void (*fn)()) {
    try {
      fn();
      ++passed;
      std::cout << "  PASS: " << name << "\n";
    } catch (const std::exception& ex) {
      ++failed;
      std::cerr << "  FAIL: " << name << " - " << ex.what() << "\n";
    }
  };

  // Banner text ties ctest logs back to WP05-T014-B.
  std::cout << "ErrorCodeEnumCompatibilityContractTest - WP05-T014-B\n";

  run_test("test_result_code_numeric_snapshot_is_stable",
           test_result_code_numeric_snapshot_is_stable);
  run_test("test_result_code_segment_boundaries_are_stable",
           test_result_code_segment_boundaries_are_stable);
  run_test("test_out_of_range_result_code_is_rejected",
           test_out_of_range_result_code_is_rejected);
  run_test("test_unknown_enum_value_downgrades_to_unspecified",
           test_unknown_enum_value_downgrades_to_unspecified);
  run_test("test_deprecated_enum_value_is_preserved_with_marker",
           test_deprecated_enum_value_is_preserved_with_marker);
  run_test("test_missing_unspecified_sentinel_is_rejected",
           test_missing_unspecified_sentinel_is_rejected);

  // Summary output follows repository convention for fast scan.
  std::cout << "\nResults: " << passed << " passed, " << failed
            << " failed, " << (passed + failed) << " total\n";

  return (failed > 0) ? 1 : 0;
}
