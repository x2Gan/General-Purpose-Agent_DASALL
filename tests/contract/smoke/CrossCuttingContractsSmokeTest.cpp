#include <array>
#include <exception>
#include <iostream>

#include "boundary/CrossCuttingContracts.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

enum class SampleLifecycle {
  Unspecified = 0,
  Active = 1,
};

void test_cross_cutting_entries_are_unified_in_single_header() {
  using dasall::contracts::CrossCuttingContracts;
  using dasall::tests::support::assert_true;

  // Positive case: a single header can provide typed entries for error/event/
  // checkpoint domains and ID/time normalization access.
  [[maybe_unused]] CrossCuttingContracts::ErrorEntry error_entry{};
  [[maybe_unused]] CrossCuttingContracts::EventEntry event_entry{};
  [[maybe_unused]] CrossCuttingContracts::CheckpointEntry checkpoint_entry{};

  const CrossCuttingContracts::TimeFields fields{
      .created_at_ms = 100,
      .deadline_at_ms = std::nullopt,
      .timeout_ms = 900,
      .timeout_seconds = std::nullopt,
  };

  const auto normalized = CrossCuttingContracts::normalize_time_fields(fields);
  assert_true(normalized.ok,
              "cross-cutting aggregate header should expose a valid time normalization entry");
}

void test_unknown_enum_value_is_downgraded_to_unspecified() {
  using dasall::contracts::CrossCuttingContracts;
  using dasall::tests::support::assert_equal;

  // Negative case: raw value 7 is outside the known value set and must be
  // downgraded to Unspecified by the enum compatibility entry.
  constexpr std::array<int, 2> kKnownValues{0, 1};
  const auto downgraded = CrossCuttingContracts::normalize_enum_with_unspecified<SampleLifecycle>(
      7,
      kKnownValues.data(),
      kKnownValues.size(),
      SampleLifecycle::Unspecified);

  assert_equal(0,
               static_cast<int>(downgraded),
               "unknown enum value should downgrade to Unspecified through the aggregate entry");
}

}  // namespace

int main() {
  try {
    test_cross_cutting_entries_are_unified_in_single_header();
    test_unknown_enum_value_is_downgraded_to_unspecified();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
