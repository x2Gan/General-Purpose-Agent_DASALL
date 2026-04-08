#include <array>
#include <exception>
#include <iostream>
#include <string>

#include "boundary/EnumLifecycleGuards.h"
#include "support/TestAssertions.h"

namespace {

dasall::contracts::EnumLifecycleDescriptor make_valid_descriptor() {
  static constexpr std::array<int, 4> kKnownValues{0, 1, 2, 3};
  static constexpr std::array<int, 1> kDeprecatedValues{3};

  return dasall::contracts::EnumLifecycleDescriptor{
      .known_values = kKnownValues.data(),
      .known_value_count = kKnownValues.size(),
      .unspecified_value = 0,
      .deprecated_values = kDeprecatedValues.data(),
      .deprecated_value_count = kDeprecatedValues.size(),
  };
}

void test_known_value_is_preserved() {
  using dasall::contracts::normalize_enum_with_lifecycle;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  // Positive case: known value should be preserved and not downgraded.
  const auto descriptor = make_valid_descriptor();
  const auto result = normalize_enum_with_lifecycle(2, descriptor);

  assert_true(result.ok, "known value normalization should succeed");
  assert_equal(2,
               result.normalized_value,
               "known value should remain unchanged after normalization");
  assert_true(!result.downgraded_from_unknown,
              "known value should not be marked as downgraded");
}

void test_unknown_value_downgrades_to_unspecified() {
  using dasall::contracts::normalize_enum_with_lifecycle;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  // Positive compatibility case: unknown value must downgrade to Unspecified.
  const auto descriptor = make_valid_descriptor();
  const auto result = normalize_enum_with_lifecycle(77, descriptor);

  assert_true(result.ok, "unknown value normalization should still succeed");
  assert_equal(0,
               result.normalized_value,
               "unknown value should downgrade to Unspecified sentinel");
  assert_true(result.downgraded_from_unknown,
              "downgrade path should be marked for compatibility evidence");
}

void test_removed_unspecified_is_rejected() {
  using dasall::contracts::EnumLifecycleDescriptor;
  using dasall::contracts::normalize_enum_with_lifecycle;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  // Negative case: deleting Unspecified from known values must fail gate.
  static constexpr std::array<int, 3> kKnownValuesWithoutUnspecified{1, 2, 3};
  const EnumLifecycleDescriptor descriptor{
      .known_values = kKnownValuesWithoutUnspecified.data(),
      .known_value_count = kKnownValuesWithoutUnspecified.size(),
      .unspecified_value = 0,
      .deprecated_values = nullptr,
      .deprecated_value_count = 0,
  };

  const auto result = normalize_enum_with_lifecycle(2, descriptor);

  assert_true(!result.ok, "descriptor without Unspecified sentinel should fail");
  assert_equal("Unspecified sentinel must be present in known_values",
               std::string(result.reason),
               "guard should block enum schema deleting Unspecified sentinel");
}

}  // namespace

int main() {
  try {
    test_known_value_is_preserved();
    test_unknown_value_downgrades_to_unspecified();
    test_removed_unspecified_is_rejected();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
