#include <array>
#include <exception>
#include <iostream>

#include "boundary/CompatibilityGuards.h"
#include "support/TestAssertions.h"

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
  assert_true(result.used_deadline_priority,
              "deadline_at_ms should mark deadline-priority path when present");
  assert_equal(8000,
               static_cast<int>(result.normalized_deadline_at_ms.value_or(0)),
               "deadline_at_ms should remain the authoritative execution deadline");
}

void test_timeout_ms_and_timeout_seconds_conflict_is_rejected() {
  using dasall::contracts::TimeoutFieldSet;
  using dasall::contracts::normalize_timeout_fields;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  // Negative case: when both timeout_ms and timeout_seconds are present,
  // inconsistent values must fail instead of silently choosing one.
  const TimeoutFieldSet fields{
      .created_at_ms = 1000,
      .deadline_at_ms = std::nullopt,
      .timeout_ms = 4500,
      .timeout_seconds = 5,
  };

  const auto result = normalize_timeout_fields(fields);

  assert_true(!result.ok, "inconsistent timeout_ms and timeout_seconds should be rejected");
  assert_equal("timeout_seconds and timeout_ms are inconsistent",
               result.reason,
               "normalizer should report double-timeout-field conflict");
}

void test_timeout_seconds_overflow_is_rejected() {
  using dasall::contracts::TimeoutFieldSet;
  using dasall::contracts::normalize_timeout_fields;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  // Negative case: migrating seconds->ms must reject uint32 overflow.
  const TimeoutFieldSet fields{
      .created_at_ms = 1000,
      .deadline_at_ms = std::nullopt,
      .timeout_ms = std::nullopt,
      .timeout_seconds = 4294968,
  };

  const auto result = normalize_timeout_fields(fields);

  assert_true(!result.ok, "overflow timeout_seconds should be rejected");
  assert_equal("timeout_seconds overflows timeout_ms",
               result.reason,
               "normalizer should report overflow during seconds-to-ms migration");
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

void test_missing_unspecified_sentinel_is_detected() {
  using dasall::contracts::has_unspecified_enum_sentinel;
  using dasall::tests::support::assert_true;

  // Negative case: if Unspecified is removed from known values, unknown-value
  // downgrade path loses its safe fallback and must be detected.
  constexpr std::array<int, 2> kKnownValuesWithoutUnspecified{1, 2};
  const bool has_unspecified = has_unspecified_enum_sentinel(kKnownValuesWithoutUnspecified.data(),
                                                             kKnownValuesWithoutUnspecified.size(),
                                                             0);

  assert_true(!has_unspecified,
              "missing Unspecified sentinel should be detected by compatibility helpers");
}

}  // namespace

int main() {
  try {
    test_timeout_seconds_migrates_to_timeout_ms();
    test_explicit_deadline_remains_authoritative();
    test_timeout_ms_and_timeout_seconds_conflict_is_rejected();
    test_timeout_seconds_overflow_is_rejected();
    test_unknown_enum_value_falls_back_to_unspecified();
    test_known_enum_value_is_preserved();
    test_missing_unspecified_sentinel_is_detected();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}