#include <exception>
#include <iostream>
#include <string>

#include "watchdog/TimeoutDecision.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_timeout_decision_accepts_escalation_shape_and_consecutive_miss_counts() {
  using dasall::infra::watchdog::TimeoutDecision;
  using dasall::infra::watchdog::WatchdogTimeoutLevel;
  using dasall::tests::support::assert_true;

  const TimeoutDecision warning{
      .entity_id = std::string("runtime.main_loop"),
      .timeout_level = WatchdogTimeoutLevel::Warning,
      .consecutive_miss = 1,
      .reason_code = dasall::contracts::ResultCode::ProviderTimeout,
      .evidence_ref = std::string("watchdog://decision/001"),
  };
  const TimeoutDecision critical{
      .entity_id = std::string("runtime.main_loop"),
      .timeout_level = WatchdogTimeoutLevel::Critical,
      .consecutive_miss = 3,
      .reason_code = dasall::contracts::ResultCode::ProviderTimeout,
      .evidence_ref = std::string("watchdog://decision/002"),
  };

  assert_true(warning.has_required_fields(),
              "TimeoutDecision should require entity_id, timeout level, consecutive_miss, reason_code, and evidence_ref");
  assert_true(critical.is_escalation_of(warning),
              "TimeoutDecision should allow unit tests to verify warning-to-critical escalation using consecutive_miss");
}

void test_timeout_decision_rejects_unspecified_levels_and_empty_evidence() {
  using dasall::infra::watchdog::TimeoutDecision;
  using dasall::tests::support::assert_true;

  const TimeoutDecision invalid{
      .entity_id = std::string("runtime.main_loop"),
      .timeout_level = dasall::infra::watchdog::WatchdogTimeoutLevel::Unspecified,
      .consecutive_miss = 0,
      .reason_code = static_cast<dasall::contracts::ResultCode>(9999),
      .evidence_ref = std::string(),
  };

  assert_true(!invalid.references_contract_reason_code(),
              "TimeoutDecision should reject reason_code values outside the frozen contracts ranges");
  assert_true(!invalid.has_required_fields(),
              "TimeoutDecision should reject unspecified levels, zero consecutive_miss, and empty evidence refs");
}

}  // namespace

int main() {
  try {
    test_timeout_decision_accepts_escalation_shape_and_consecutive_miss_counts();
    test_timeout_decision_rejects_unspecified_levels_and_empty_evidence();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}