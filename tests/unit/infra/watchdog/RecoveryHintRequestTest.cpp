#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "watchdog/RecoveryHintRequest.h"
#include "support/TestAssertions.h"

namespace {

void test_recovery_hint_request_accepts_required_advisory_fields() {
  using dasall::contracts::ResultCode;
  using dasall::infra::watchdog::RecoveryHintRequest;
  using dasall::tests::support::assert_true;

  static_assert(
      std::is_same_v<decltype(RecoveryHintRequest{}.target_ref), std::string>);
  static_assert(std::is_same_v<decltype(RecoveryHintRequest{}.suggested_action),
                               std::string>);
  static_assert(std::is_same_v<decltype(RecoveryHintRequest{}.evidence_ref),
                               std::string>);

  const RecoveryHintRequest request{
      .reason_code = ResultCode::ProviderTimeout,
      .target_ref = "runtime.main_loop",
      .suggested_action = "quarantine_and_restart_target",
      .evidence_ref = "watchdog://decision/runtime.main_loop/critical",
  };

  assert_true(
      request.references_contract_reason_code(),
      "RecoveryHintRequest should keep reason_code inside the frozen contracts ResultCode ranges");
  assert_true(
      request.has_required_fields(),
      "RecoveryHintRequest should require reason_code, target_ref, suggested_action, and evidence_ref before admission");
}

void test_recovery_hint_request_rejects_missing_advisory_fields_and_unknown_reason_codes() {
  using dasall::infra::watchdog::RecoveryHintRequest;
  using dasall::tests::support::assert_true;

  const RecoveryHintRequest default_request{};
  const RecoveryHintRequest invalid{
      .reason_code = static_cast<dasall::contracts::ResultCode>(9999),
      .target_ref = std::string(),
      .suggested_action = std::string(),
      .evidence_ref = std::string(),
  };

  assert_true(
      !default_request.has_required_fields(),
      "default-constructed RecoveryHintRequest should stay invalid until all advisory fields are specified");
  assert_true(
      !invalid.references_contract_reason_code(),
      "RecoveryHintRequest should reject reason codes outside the frozen contracts ranges");
  assert_true(
      !invalid.has_required_fields(),
      "RecoveryHintRequest should reject empty advisory fields when reason_code is unknown");
}

}  // namespace

int main() {
  try {
    test_recovery_hint_request_accepts_required_advisory_fields();
    test_recovery_hint_request_rejects_missing_advisory_fields_and_unknown_reason_codes();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}