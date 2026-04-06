#include <exception>
#include <iostream>
#include <string>

#include "health/RecoveryHintEmitter.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::infra::HealthSnapshot make_degraded_snapshot() {
  return dasall::infra::HealthSnapshot{
      .liveness = true,
      .readiness = false,
      .degraded = true,
      .failed_components = {"logging_sink"},
      .version = 7,
      .timestamp = 1712443200000,
  };
}

dasall::infra::HealthSnapshot make_unhealthy_snapshot() {
  return dasall::infra::HealthSnapshot{
      .liveness = false,
      .readiness = false,
      .degraded = false,
      .failed_components = {"secret_backend", "config_center"},
      .version = 9,
      .timestamp = 1712443200100,
  };
}

void test_recovery_hint_emitter_emits_advisory_for_degraded_snapshot() {
  using dasall::contracts::ResultCode;
  using dasall::infra::RecoveryHintEmitter;
  using dasall::infra::RecoveryHintSeverity;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  RecoveryHintEmitter emitter;
  const auto result = emitter.emit_hint(make_degraded_snapshot(),
                                        "probe timeout: logging/sink");

  assert_true(result.ok, "RecoveryHintEmitter should emit an advisory hint for degraded snapshots");
  assert_true(result.hint.has_required_fields(),
              "RecoveryHintEmitter should keep emitted advisory hints structurally valid");
  assert_true(result.hint.reason_code == ResultCode::ProviderTimeout,
              "RecoveryHintEmitter should map degraded snapshots to a retryable provider failure reason");
  assert_true(result.hint.severity == RecoveryHintSeverity::Warning,
              "RecoveryHintEmitter should keep degraded hints below runtime recovery execution severity");
  assert_equal(std::string("observe_and_retry_later"),
               result.hint.suggested_action,
               "RecoveryHintEmitter should keep the degraded suggested_action stable");
  assert_equal(std::string("audit://health/recovery_hint/degraded/version/7/components/logging_sink/reason/probe_timeout__logging_sink"),
               result.hint.evidence_ref,
               "RecoveryHintEmitter should keep degraded evidence refs traceable and sanitized");
}

void test_recovery_hint_emitter_escalates_unhealthy_snapshot_without_execution_handle() {
  using dasall::contracts::ResultCode;
  using dasall::infra::RecoveryHintEmitter;
  using dasall::infra::RecoveryHintSeverity;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  RecoveryHintEmitter emitter;
  const auto result = emitter.emit_hint(make_unhealthy_snapshot(),
                                        "consecutive failures");

  assert_true(result.ok,
              "RecoveryHintEmitter should emit an advisory hint for unhealthy snapshots");
  assert_true(result.hint.reason_code == ResultCode::RuntimeRetryExhausted,
              "RecoveryHintEmitter should map unhealthy snapshots to a runtime exhaustion reason");
  assert_true(result.hint.severity == RecoveryHintSeverity::Critical,
              "RecoveryHintEmitter should escalate unhealthy snapshots to critical advisory severity");
  assert_equal(std::string("escalate_for_runtime_recovery_review"),
               result.hint.suggested_action,
               "RecoveryHintEmitter should keep unhealthy suggested_action advisory rather than executable");
  assert_equal(std::string("audit://health/recovery_hint/unhealthy/version/9/components/secret_backend__config_center/reason/consecutive_failures"),
               result.hint.evidence_ref,
               "RecoveryHintEmitter should include the failed component set in unhealthy advisory evidence refs");
}

void test_recovery_hint_emitter_rejects_healthy_snapshot_and_sanitizes_reason_payloads() {
  using dasall::contracts::ResultCode;
  using dasall::infra::HealthSnapshot;
  using dasall::infra::RecoveryHintEmitter;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  RecoveryHintEmitter emitter;
  const HealthSnapshot healthy_snapshot{
      .liveness = true,
      .readiness = true,
      .degraded = false,
      .failed_components = {},
      .version = 5,
      .timestamp = 1712443200200,
  };

  const auto rejected = emitter.emit_hint(healthy_snapshot, "healthy state");

  assert_true(!rejected.ok && rejected.references_only_contract_error_types() &&
                  rejected.result_code.has_value() &&
                  *rejected.result_code == ResultCode::ValidationFieldMissing,
              "RecoveryHintEmitter should reject healthy snapshots so health does not overstep into proactive execution orchestration");
  assert_equal(std::string("runtime_recovery_review_1"),
               emitter.sanitize_hint_payload("runtime recovery/review#1"),
               "RecoveryHintEmitter should sanitize advisory payload fragments before writing evidence refs");
}

}  // namespace

int main() {
  try {
    test_recovery_hint_emitter_emits_advisory_for_degraded_snapshot();
    test_recovery_hint_emitter_escalates_unhealthy_snapshot_without_execution_handle();
    test_recovery_hint_emitter_rejects_healthy_snapshot_and_sanitizes_reason_payloads();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}