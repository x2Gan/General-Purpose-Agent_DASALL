#include <array>
#include <exception>
#include <iostream>

#include "dasall/contracts/CompatibilityGuards.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

enum class SampleStatus {
  Unspecified = 0,
  Active = 1,
  Deprecated = 2,
};

void test_timeout_seconds_migrates_to_timeout_ms() {
  using dasall::contracts::TimeoutFieldSet;
  using dasall::contracts::normalize_timeout_fields;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const TimeoutFieldSet fields{
      .created_at_ms = 1000,
      .deadline_at_ms = std::nullopt,
      .timeout_ms = std::nullopt,
      .timeout_seconds = 5,
  };

  const auto result = normalize_timeout_fields(fields);

  assert_true(result.ok, "legacy timeout_seconds should be normalized successfully");
  assert_true(result.used_legacy_timeout_seconds,
              "normalization should report when legacy seconds field was used");
  assert_equal(5000,
               static_cast<int>(result.normalized_timeout_ms.value_or(0)),
               "timeout_seconds should be migrated to timeout_ms");
  assert_equal(6000,
               static_cast<int>(result.normalized_deadline_at_ms.value_or(0)),
               "created_at_ms plus timeout_ms should derive deadline_at_ms");
}

void test_explicit_deadline_remains_authoritative() {
  using dasall::contracts::TimeoutFieldSet;
  using dasall::contracts::normalize_timeout_fields;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const TimeoutFieldSet fields{
      .created_at_ms = 1000,
      .deadline_at_ms = 8000,
      .timeout_ms = 5000,
      .timeout_seconds = std::nullopt,
  };

  const auto result = normalize_timeout_fields(fields);

  assert_true(result.ok, "explicit deadline_at_ms should keep the object valid");
  assert_equal(8000,
               static_cast<int>(result.normalized_deadline_at_ms.value_or(0)),
               "deadline_at_ms should remain the authoritative execution deadline");
}

void test_unknown_enum_value_falls_back_to_unspecified() {
  using dasall::contracts::fallback_unknown_enum_value;
  using dasall::tests::support::assert_equal;

  constexpr std::array<int, 3> kKnownValues{0, 1, 2};
  const auto value = fallback_unknown_enum_value<SampleStatus>(7,
                                                               kKnownValues.data(),
                                                               kKnownValues.size(),
                                                               SampleStatus::Unspecified);

  assert_equal(0,
               static_cast<int>(value),
               "unknown enum values should fall back to Unspecified");
}

void test_known_enum_value_is_preserved() {
  using dasall::contracts::fallback_unknown_enum_value;
  using dasall::tests::support::assert_equal;

  constexpr std::array<int, 3> kKnownValues{0, 1, 2};
  const auto value = fallback_unknown_enum_value<SampleStatus>(1,
                                                               kKnownValues.data(),
                                                               kKnownValues.size(),
                                                               SampleStatus::Unspecified);

  assert_equal(1,
               static_cast<int>(value),
               "known enum values should not be downgraded");
}

}  // namespace

int main() {
  try {
    test_timeout_seconds_migrates_to_timeout_ms();
    test_explicit_deadline_remains_authoritative();
    test_unknown_enum_value_falls_back_to_unspecified();
    test_known_enum_value_is_preserved();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}