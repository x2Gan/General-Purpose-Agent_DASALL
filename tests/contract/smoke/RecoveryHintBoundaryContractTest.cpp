#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "../../../infra/include/health/RecoveryHint.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

template <typename T>
concept HasExecutedActionMember = requires {
  &T::executed_action;
};

template <typename T>
concept HasCheckpointRefMember = requires {
  &T::checkpoint_ref;
};

template <typename T>
concept HasUpdatedRetryCountMember = requires {
  &T::updated_retry_count;
};

template <typename T>
concept HasCompensationResultRefMember = requires {
  &T::compensation_result_ref;
};

void test_recovery_hint_keeps_advisory_fields_without_execution_handles() {
  using dasall::contracts::ResultCode;
  using dasall::infra::RecoveryHint;
  using dasall::infra::RecoveryHintSeverity;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(RecoveryHint{}.reason_code), ResultCode>);
  static_assert(std::is_same_v<decltype(RecoveryHint{}.severity), RecoveryHintSeverity>);
  static_assert(std::is_same_v<decltype(RecoveryHint{}.suggested_action), std::string>);
  static_assert(std::is_same_v<decltype(RecoveryHint{}.evidence_ref), std::string>);
  static_assert(!HasExecutedActionMember<RecoveryHint>);
  static_assert(!HasCheckpointRefMember<RecoveryHint>);
  static_assert(!HasUpdatedRetryCountMember<RecoveryHint>);
  static_assert(!HasCompensationResultRefMember<RecoveryHint>);

  const RecoveryHint hint{
      .reason_code = ResultCode::ProviderTimeout,
      .severity = RecoveryHintSeverity::Warning,
      .suggested_action = "observe_and_retry_later",
      .evidence_ref = "health://transition/001",
  };

  assert_true(hint.has_required_fields(),
              "RecoveryHint should remain an advisory object with required fields but without execution handles");
}

void test_recovery_hint_rejects_empty_advisory_payload() {
  using dasall::infra::RecoveryHint;
  using dasall::tests::support::assert_true;

  const RecoveryHint hint{};

  assert_true(!hint.has_required_fields(),
              "RecoveryHint should reject empty advisory payloads so future emitters cannot claim contract readiness prematurely");
}

}  // namespace

int main() {
  try {
    test_recovery_hint_keeps_advisory_fields_without_execution_handles();
    test_recovery_hint_rejects_empty_advisory_payload();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}