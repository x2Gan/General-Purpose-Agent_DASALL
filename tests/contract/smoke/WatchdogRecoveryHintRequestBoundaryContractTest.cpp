#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "watchdog/RecoveryHintRequest.h"
#include "support/TestAssertions.h"

namespace {

template <typename T>
concept HasExecutedActionMember = requires {
  &T::executed_action;
};

template <typename T>
concept HasRetryAfterMsMember = requires {
  &T::retry_after_ms;
};

template <typename T>
concept HasCheckpointRefMember = requires {
  &T::checkpoint_ref;
};

template <typename T>
concept HasRuntimeBudgetSnapshotMember = requires {
  &T::runtime_budget_snapshot;
};

template <typename T>
concept HasIdempotencyAndSideEffectReportMember = requires {
  &T::idempotency_and_side_effect_report;
};

void test_recovery_hint_request_keeps_advisory_fields_without_execution_handles() {
  using dasall::contracts::ResultCode;
  using dasall::infra::watchdog::RecoveryHintRequest;
  using dasall::tests::support::assert_true;

  static_assert(
      std::is_same_v<decltype(RecoveryHintRequest{}.reason_code), ResultCode>);
  static_assert(
      std::is_same_v<decltype(RecoveryHintRequest{}.target_ref), std::string>);
  static_assert(std::is_same_v<decltype(RecoveryHintRequest{}.suggested_action),
                               std::string>);
  static_assert(std::is_same_v<decltype(RecoveryHintRequest{}.evidence_ref),
                               std::string>);
  static_assert(!HasExecutedActionMember<RecoveryHintRequest>);
  static_assert(!HasRetryAfterMsMember<RecoveryHintRequest>);
  static_assert(!HasCheckpointRefMember<RecoveryHintRequest>);
  static_assert(!HasRuntimeBudgetSnapshotMember<RecoveryHintRequest>);
  static_assert(!HasIdempotencyAndSideEffectReportMember<RecoveryHintRequest>);

  const RecoveryHintRequest request{
      .reason_code = ResultCode::ProviderTimeout,
      .target_ref = "runtime.main_loop",
      .suggested_action = "escalate_to_recovery_manager",
      .evidence_ref = "watchdog://timeout/runtime.main_loop/critical",
  };

  assert_true(
      request.has_required_fields(),
      "RecoveryHintRequest should remain an advisory-only object with required fields but without runtime execution handles");
}

void test_recovery_hint_request_rejects_empty_advisory_payload() {
  using dasall::infra::watchdog::RecoveryHintRequest;
  using dasall::tests::support::assert_true;

  const RecoveryHintRequest request{};

  assert_true(
      !request.has_required_fields(),
      "RecoveryHintRequest should reject empty payloads so future emitters cannot bypass advisory boundary checks");
}

}  // namespace

int main() {
  try {
    test_recovery_hint_request_keeps_advisory_fields_without_execution_handles();
    test_recovery_hint_request_rejects_empty_advisory_payload();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}