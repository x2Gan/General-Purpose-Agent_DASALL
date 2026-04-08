#include <exception>
#include <iostream>
#include <string>

#include "watchdog/RecoveryRequestEmitter.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::infra::watchdog::TimeoutDecision make_timeout_decision(
    dasall::infra::watchdog::WatchdogTimeoutLevel timeout_level,
    std::uint32_t consecutive_miss,
    std::string evidence_ref = "watchdog://timeout/runtime.main_loop/critical") {
  return dasall::infra::watchdog::TimeoutDecision{
      .entity_id = std::string("runtime.main_loop"),
      .timeout_level = timeout_level,
      .consecutive_miss = consecutive_miss,
      .reason_code = dasall::contracts::ResultCode::ProviderTimeout,
      .evidence_ref = std::move(evidence_ref),
  };
}

void test_recovery_request_emitter_emits_critical_advisory_request() {
  using dasall::infra::watchdog::RecoveryRequestEmitter;
  using dasall::infra::watchdog::WatchdogTimeoutLevel;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  RecoveryRequestEmitter emitter;
  const auto result = emitter.emit_recovery_hint(
      make_timeout_decision(WatchdogTimeoutLevel::Critical, 3));

  assert_true(result.ok,
              "RecoveryRequestEmitter should emit advisory-only recovery requests for critical timeout decisions");
  assert_true(result.request.has_required_fields(),
              "RecoveryRequestEmitter should keep emitted recovery requests structurally valid");
  assert_equal(std::string("runtime.main_loop"),
               result.request.target_ref,
               "RecoveryRequestEmitter should target the timed out entity without introducing execution handles");
  assert_equal(std::string("review_runtime_recovery_for_target"),
               result.request.suggested_action,
               "RecoveryRequestEmitter should keep critical timeout actions advisory-only");
  assert_equal(std::string("audit://watchdog/recovery_hint/runtime_main_loop/critical/miss/3/decision/watchdog___timeout_runtime_main_loop_critical"),
               result.request.evidence_ref,
               "RecoveryRequestEmitter should preserve entity, timeout level, miss count, and sanitized decision evidence in evidence_ref");
}

void test_recovery_request_emitter_escalates_fatal_timeout_reviews() {
  using dasall::infra::watchdog::RecoveryRequestEmitter;
  using dasall::infra::watchdog::WatchdogTimeoutLevel;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  RecoveryRequestEmitter emitter;
  const auto result = emitter.emit_recovery_hint(
      make_timeout_decision(WatchdogTimeoutLevel::Fatal,
                            5,
                            "watchdog://timeout/runtime.main_loop/fatal"));

  assert_true(result.ok,
              "RecoveryRequestEmitter should keep fatal timeout escalation inside advisory review output");
  assert_equal(std::string("escalate_for_runtime_recovery_review"),
               result.request.suggested_action,
               "RecoveryRequestEmitter should map fatal timeout decisions to the stronger advisory action string");
  assert_equal(std::string("audit://watchdog/recovery_hint/runtime_main_loop/fatal/miss/5/decision/watchdog___timeout_runtime_main_loop_fatal"),
               result.request.evidence_ref,
               "RecoveryRequestEmitter should keep fatal evidence refs traceable after sanitization");
}

void test_recovery_request_emitter_rejects_warning_decisions_and_sanitizes_payloads() {
  using dasall::contracts::ResultCode;
  using dasall::infra::watchdog::RecoveryRequestEmitter;
  using dasall::infra::watchdog::WatchdogTimeoutLevel;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  RecoveryRequestEmitter emitter;
  const auto rejected = emitter.emit_recovery_hint(
      make_timeout_decision(WatchdogTimeoutLevel::Warning, 1));

  assert_true(!rejected.ok && rejected.references_only_contract_error_types() &&
                  rejected.result_code.has_value() &&
                  *rejected.result_code == ResultCode::ValidationFieldMissing,
              "RecoveryRequestEmitter should reject warning-only timeout decisions so watchdog does not overstep into premature recovery orchestration");
  assert_equal(std::string("runtime_recovery_review_1"),
               emitter.sanitize_payload("runtime recovery/review#1"),
               "RecoveryRequestEmitter should sanitize payload fragments before using them inside advisory evidence refs");
}

}  // namespace

int main() {
  try {
    test_recovery_request_emitter_emits_critical_advisory_request();
    test_recovery_request_emitter_escalates_fatal_timeout_reviews();
    test_recovery_request_emitter_rejects_warning_decisions_and_sanitizes_payloads();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}