#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "health/RecoveryHint.h"
#include "support/TestAssertions.h"

namespace {

void test_recovery_hint_accepts_required_advisory_fields() {
  using dasall::contracts::ResultCode;
  using dasall::infra::RecoveryHint;
  using dasall::infra::RecoveryHintSeverity;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(RecoveryHint{}.suggested_action), std::string>);
  static_assert(std::is_same_v<decltype(RecoveryHint{}.severity), RecoveryHintSeverity>);

  const RecoveryHint hint{
      .reason_code = ResultCode::ProviderTimeout,
      .severity = RecoveryHintSeverity::Critical,
      .suggested_action = "observe_and_escalate",
      .evidence_ref = "health://snapshot/001",
  };

  assert_true(hint.references_contract_reason_code(),
              "RecoveryHint should keep its reason_code inside the frozen contracts error ranges");
  assert_true(hint.has_required_fields(),
              "RecoveryHint should require reason_code, severity, suggested_action, and evidence_ref before admission");
}

void test_recovery_hint_rejects_unspecified_fields_and_unknown_reason_codes() {
  using dasall::infra::RecoveryHint;
  using dasall::infra::RecoveryHintSeverity;
  using dasall::tests::support::assert_true;

  const RecoveryHint default_hint{};

  const RecoveryHint missing_fields{
      .reason_code = static_cast<dasall::contracts::ResultCode>(9999),
      .severity = RecoveryHintSeverity::Unspecified,
      .suggested_action = std::string(),
      .evidence_ref = std::string(),
  };

  assert_true(!default_hint.has_required_fields(),
              "default-constructed RecoveryHint should stay invalid until all advisory fields are specified");
  assert_true(!missing_fields.references_contract_reason_code(),
              "RecoveryHint should reject reason codes outside the frozen contracts ranges");
  assert_true(!missing_fields.has_required_fields(),
              "RecoveryHint should reject unspecified severity and empty advisory fields");
}

}  // namespace

int main() {
  try {
    test_recovery_hint_accepts_required_advisory_fields();
    test_recovery_hint_rejects_unspecified_fields_and_unknown_reason_codes();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}