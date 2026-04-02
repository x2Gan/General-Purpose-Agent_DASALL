#include <exception>
#include <iostream>
#include <string>

#include "watchdog/TimeoutDecision.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_timeout_decision_reason_code_stays_inside_contract_ranges() {
  using dasall::infra::watchdog::TimeoutDecision;
  using dasall::infra::watchdog::WatchdogTimeoutLevel;
  using dasall::tests::support::assert_true;

  const TimeoutDecision decision{
      .entity_id = std::string("runtime.main_loop"),
      .timeout_level = WatchdogTimeoutLevel::Critical,
      .consecutive_miss = 3,
      .reason_code = dasall::contracts::ResultCode::ProviderTimeout,
      .evidence_ref = std::string("watchdog://decision/critical"),
  };

  assert_true(decision.references_contract_reason_code(),
              "TimeoutDecision should keep reason_code inside the frozen contracts ResultCode ranges");
  assert_true(decision.has_required_fields(),
              "TimeoutDecision should remain contract-safe when reason_code maps to a known contracts category");
}

void test_timeout_decision_rejects_unknown_reason_code_boundaries() {
  using dasall::infra::watchdog::TimeoutDecision;
  using dasall::infra::watchdog::WatchdogTimeoutLevel;
  using dasall::tests::support::assert_true;

  const TimeoutDecision invalid{
      .entity_id = std::string("runtime.main_loop"),
      .timeout_level = WatchdogTimeoutLevel::Fatal,
      .consecutive_miss = 5,
      .reason_code = static_cast<dasall::contracts::ResultCode>(9999),
      .evidence_ref = std::string("watchdog://decision/invalid"),
  };

  assert_true(!invalid.references_contract_reason_code(),
              "TimeoutDecision should reject unknown reason_code values at the contract boundary");
  assert_true(!invalid.has_required_fields(),
              "TimeoutDecision contract boundary should fail closed when reason_code escapes the frozen contracts ranges");
}

}  // namespace

int main() {
  try {
    test_timeout_decision_reason_code_stays_inside_contract_ranges();
    test_timeout_decision_rejects_unknown_reason_code_boundaries();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}