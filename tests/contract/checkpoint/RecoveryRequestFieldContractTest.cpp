// ============================================================================
// RecoveryRequestFieldContractTest.cpp
//
// WP04-T010-B: Field-level contract test for RecoveryRequestGuards.h.
//
// Validates the T010 field-table rules layered on top of the T009 object guard:
//   - latest_observation must satisfy source→correlation rules.
//   - request_id / goal_id remain aligned across nested evidence.
//   - error_info mirrors latest_observation.error and keeps source_ref aligned.
//   - replay_safe=true with observed side-effects requires an idempotency_key.
// ============================================================================

#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "checkpoint/RecoveryRequest.h"
#include "checkpoint/RecoveryRequestGuards.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::BudgetSnapshot;
using dasall::contracts::BudgetSnapshotEntry;
using dasall::contracts::BudgetType;
using dasall::contracts::Checkpoint;
using dasall::contracts::CheckpointState;
using dasall::contracts::ErrorInfo;
using dasall::contracts::IdempotencyAndSideEffectReport;
using dasall::contracts::Observation;
using dasall::contracts::ObservationSource;
using dasall::contracts::RecoveryRequest;
using dasall::contracts::ReflectionDecision;
using dasall::contracts::ReflectionDecisionKind;
using dasall::contracts::ResultCodeCategory;
using dasall::contracts::validate_recovery_request_field_rules;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

ReflectionDecision make_valid_reflection_decision() {
  ReflectionDecision decision;
  decision.request_id = "req-010";
  decision.goal_id = "goal-010";
  decision.decision_kind = ReflectionDecisionKind::RetryStep;
  decision.rationale = "transient tool failure still has a safe replay path";
  decision.confidence = 0.75F;
  decision.relevant_observation_refs = std::vector<std::string>{"obs-010"};
  decision.hint_ref = "hint:retry:tool";
  decision.created_at = 1710115200000;
  return decision;
}

ErrorInfo make_valid_error_info() {
  ErrorInfo error_info;
  error_info.failure_type = ResultCodeCategory::Tool;
  error_info.retryable = true;
  error_info.safe_to_replan = true;
  error_info.details.code = 503;
  error_info.details.message = "tool backend unavailable";
  error_info.details.stage = "tool_execution";
  error_info.source_ref.ref_type = "tool_call";
  error_info.source_ref.ref_id = "tool-call-010";
  return error_info;
}

Observation make_valid_observation() {
  Observation observation;
  observation.observation_id = "obs-010";
  observation.source = ObservationSource::ToolExecution;
  observation.success = false;
  observation.payload = "{\"status\":\"failed\"}";
  observation.created_at = 1710115200100;
  observation.error = make_valid_error_info();
  observation.side_effects = std::vector<std::string>{"remote-session-opened"};
  observation.tool_call_id = "tool-call-010";
  observation.request_id = "req-010";
  observation.goal_id = "goal-010";
  observation.duration_ms = 1800;
  return observation;
}

Checkpoint make_valid_checkpoint() {
  Checkpoint checkpoint;
  checkpoint.checkpoint_id = "cp-010";
  checkpoint.state = CheckpointState::Failed;
  checkpoint.step_id = "step-7";
  checkpoint.working_memory_snapshot = "wm:cp:010";
  checkpoint.pending_action = "retry tool execution after admission";
  checkpoint.request_id = "req-010";
  checkpoint.goal_id = "goal-010";
  checkpoint.retry_count = 2U;
  checkpoint.created_at = 1710115200200;
  checkpoint.tags = std::vector<std::string>{"recovery", "tool"};
  return checkpoint;
}

IdempotencyAndSideEffectReport make_valid_report() {
  IdempotencyAndSideEffectReport report;
  report.replay_safe = false;
  report.idempotency_key = "idem-010";
  report.side_effects_present = true;
  report.non_replayable_reason = "tool already emitted irreversible side effect";
  return report;
}

BudgetSnapshot make_valid_budget_snapshot() {
  BudgetSnapshot snapshot;
  snapshot.snapshot_at_ms = 1710115200300;
  snapshot.entries.push_back(BudgetSnapshotEntry{
      .budget_type = BudgetType::ToolCall,
      .current = 2,
      .max = 5,
      .remaining = 3,
      .reject_reason = std::nullopt,
  });
  snapshot.entries.push_back(BudgetSnapshotEntry{
      .budget_type = BudgetType::Latency,
      .current = 1800,
      .max = 5000,
      .remaining = 3200,
      .reject_reason = std::nullopt,
  });
  return snapshot;
}

RecoveryRequest make_valid_request() {
  RecoveryRequest request;
  request.reflection_decision = make_valid_reflection_decision();
  request.error_info = make_valid_error_info();
  request.latest_observation = make_valid_observation();
  request.checkpoint = make_valid_checkpoint();
  request.idempotency_and_side_effect_report = make_valid_report();
  request.retry_count = 2U;
  request.runtime_budget_snapshot = make_valid_budget_snapshot();
  return request;
}

void test_valid_request_passes_field_rules() {
  const auto request = make_valid_request();
  const auto result = validate_recovery_request_field_rules(request);

  assert_true(result.ok,
              "valid RecoveryRequest should pass field rules");
}

void test_request_id_mismatch_rejected() {
  auto request = make_valid_request();
  request.latest_observation->request_id = "req-other";

  const auto result = validate_recovery_request_field_rules(request);
  assert_true(!result.ok, "request_id mismatch must be rejected");
  assert_equal(
      "request_id values across reflection_decision/latest_observation/checkpoint must match when present",
      std::string(result.reason),
      "request_id mismatch must return canonical reason");
}

void test_goal_id_mismatch_rejected() {
  auto request = make_valid_request();
  request.checkpoint->goal_id = "goal-other";

  const auto result = validate_recovery_request_field_rules(request);
  assert_true(!result.ok, "goal_id mismatch must be rejected");
  assert_equal(
      "goal_id values across reflection_decision/latest_observation/checkpoint must match when present",
      std::string(result.reason),
      "goal_id mismatch must return canonical reason");
}

void test_error_info_drift_rejected() {
  auto request = make_valid_request();
  request.error_info->details.code = 504;

  const auto result = validate_recovery_request_field_rules(request);
  assert_true(!result.ok, "error_info drift must be rejected");
  assert_equal(
      "error_info must mirror latest_observation.error when latest_observation.error is present",
      std::string(result.reason),
      "error_info drift must return canonical reason");
}

void test_tool_source_ref_alignment_rejected() {
  auto request = make_valid_request();
  request.error_info->source_ref.ref_id = "tool-call-other";
  request.latest_observation->error->source_ref.ref_id = "tool-call-other";

  const auto result = validate_recovery_request_field_rules(request);
  assert_true(!result.ok, "tool source_ref misalignment must be rejected");
  assert_equal(
      "error_info.source_ref.ref_id must match latest_observation.tool_call_id for ToolExecution observations",
      std::string(result.reason),
      "tool source_ref misalignment must return canonical reason");
}

void test_missing_idempotency_key_for_safe_replay_with_side_effects_rejected() {
  auto request = make_valid_request();
  request.idempotency_and_side_effect_report->replay_safe = true;
  request.idempotency_and_side_effect_report->side_effects_present = true;
  request.idempotency_and_side_effect_report->idempotency_key = std::nullopt;
  request.idempotency_and_side_effect_report->non_replayable_reason = std::nullopt;

  const auto result = validate_recovery_request_field_rules(request);
  assert_true(!result.ok,
              "safe replay with observed side effects requires idempotency_key");
  assert_equal(
      "idempotency_key is required when replay_safe is true and side_effects_present is true",
      std::string(result.reason),
      "missing idempotency_key must return canonical reason");
}

void test_observation_source_correlation_failure_is_inherited() {
  auto request = make_valid_request();
  request.latest_observation->tool_call_id = std::nullopt;

  const auto result = validate_recovery_request_field_rules(request);
  assert_true(!result.ok,
              "observation source correlation failure must be inherited");
  assert_equal("tool_call_id is required when source is ToolExecution",
               std::string(result.reason),
               "inherited observation correlation failure must preserve canonical reason");
}

}  // namespace

int main() {
  try {
    test_valid_request_passes_field_rules();
    test_request_id_mismatch_rejected();
    test_goal_id_mismatch_rejected();
    test_error_info_drift_rejected();
    test_tool_source_ref_alignment_rejected();
    test_missing_idempotency_key_for_safe_replay_with_side_effects_rejected();
    test_observation_source_correlation_failure_is_inherited();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}