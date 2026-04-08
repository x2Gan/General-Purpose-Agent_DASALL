#include <exception>
#include <iostream>
#include <string>

#include "boundary/TimeDeadlineGuards.h"
#include "support/TestAssertions.h"

namespace {

void test_consistent_deadline_and_timeout_are_accepted() {
  using dasall::contracts::TimeoutFieldSet;
  using dasall::contracts::validate_time_deadline_fields;
  using dasall::tests::support::assert_true;

  // Positive case: deadline_at_ms is present and consistent with created_at_ms
  // plus timeout_ms, so validation should pass and report deadline priority.
  const TimeoutFieldSet fields{
      .created_at_ms = 1000,
      .deadline_at_ms = 7000,
      .timeout_ms = 6000,
      .timeout_seconds = std::nullopt,
  };

  const auto result = validate_time_deadline_fields(fields);

  assert_true(result.ok, "consistent deadline and timeout fields should pass validation");
  assert_true(result.used_deadline_priority,
              "deadline field presence should trigger deadline-priority semantics");
}

void test_deadline_conflict_with_timeout_is_rejected() {
  using dasall::contracts::TimeoutFieldSet;
  using dasall::contracts::validate_time_deadline_fields;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  // Negative case: deadline_at_ms conflicts with created_at_ms + timeout_ms
  // and must fail to prevent ambiguous timeout/deadline behavior.
  const TimeoutFieldSet fields{
      .created_at_ms = 1000,
      .deadline_at_ms = 9000,
      .timeout_ms = 6000,
      .timeout_seconds = std::nullopt,
  };

  const auto result = validate_time_deadline_fields(fields);

  assert_true(!result.ok, "deadline/timeout conflict should be rejected");
  assert_equal("deadline_at_ms conflicts with created_at_ms plus timeout_ms",
               std::string(result.reason),
               "time deadline guard should expose explicit conflict reason");
}

}  // namespace

int main() {
  try {
    test_consistent_deadline_and_timeout_are_accepted();
    test_deadline_conflict_with_timeout_is_rejected();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
