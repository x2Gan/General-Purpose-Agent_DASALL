// ============================================================================
// RecoveryRequestContractTest.cpp
//
// WP04-T009-B: Contract test for RecoveryRequest.h and RecoveryRequestGuards.h.
//
// Validates that RecoveryRequest remains a runtime-owned admission object:
//   - Required nested evidence is present and structurally valid.
//   - Boundary rules reject success observations, succeeded checkpoints, and
//     inconsistent retry counters.
//   - Top-level field guards reject second-reflection semantics, outcome
//     result fields, and execution scheduling outputs.
//
// Verification command (WP04-T009):
//   cmake --build build-ci --target dasall_contract_tests &&
//   ctest --test-dir build-ci -R RecoveryRequestContractTest --output-on-failure
// ============================================================================

#include <exception>
#include <iostream>
#include <string>

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
using dasall::contracts::RecoveryRequestBoundaryDecision;
using dasall::contracts::ReflectionDecision;
using dasall::contracts::ReflectionDecisionKind;
using dasall::contracts::ResultCodeCategory;
using dasall::contracts::validate_recovery_request_boundary;
using dasall::contracts::validate_recovery_request_contract_field_boundary;
using dasall::contracts::validate_recovery_request_required_fields;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

ReflectionDecision make_valid_reflection_decision() {
  ReflectionDecision decision;
  decision.request_id = "req-009";
  decision.decision_kind = ReflectionDecisionKind::RetryStep;
  decision.rationale = "transient tool failure suggests one controlled replay";
  decision.confidence = 0.8F;
  decision.relevant_observation_refs = std::vector<std::string>{"obs-009"};
  decision.hint_ref = "hint:recovery:tool-retry";
  decision.created_at = 1710000900000;
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
  error_info.source_ref.ref_id = "tool-call-009";
  return error_info;
}

Observation make_valid_observation() {
  Observation observation;
  observation.observation_id = "obs-009";
  observation.source = ObservationSource::ToolExecution;
  observation.success = false;
  observation.payload = "{\"status\":\"failed\"}";
  observation.created_at = 1710000900100;
  observation.error = make_valid_error_info();
  observation.side_effects = std::vector<std::string>{"opened remote session"};
  observation.tool_call_id = "tool-call-009";
  observation.request_id = "req-009";
  observation.goal_id = "goal-009";
  observation.duration_ms = 2500;
  return observation;
}

Checkpoint make_valid_checkpoint() {
  Checkpoint checkpoint;
  checkpoint.checkpoint_id = "cp-009";
  checkpoint.state = CheckpointState::Failed;
  checkpoint.step_id = "step-4";
  checkpoint.working_memory_snapshot = "wm:checkpoint:009";
  checkpoint.pending_action = "retry tool execution after admission";
  checkpoint.request_id = "req-009";
  checkpoint.goal_id = "goal-009";
  checkpoint.retry_count = 1U;
  checkpoint.created_at = 1710000900200;
  checkpoint.tags = std::vector<std::string>{"recovery", "tool"};
  return checkpoint;
}

IdempotencyAndSideEffectReport make_valid_report() {
  IdempotencyAndSideEffectReport report;
  report.replay_safe = false;
  report.idempotency_key = "idem-009";
  report.side_effects_present = true;
  report.non_replayable_reason = "remote side effect already observed";
  return report;
}

BudgetSnapshot make_valid_budget_snapshot() {
  BudgetSnapshot snapshot;
  snapshot.snapshot_at_ms = 1710000900300;
  snapshot.entries.push_back(BudgetSnapshotEntry{
      .budget_type = BudgetType::ToolCall,
      .current = 2,
      .max = 4,
      .remaining = 2,
      .reject_reason = std::nullopt,
  });
  snapshot.entries.push_back(BudgetSnapshotEntry{
      .budget_type = BudgetType::Latency,
      .current = 2400,
      .max = 5000,
      .remaining = 2600,
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
  request.retry_count = 1U;
  request.runtime_budget_snapshot = make_valid_budget_snapshot();
  return request;
}

void test_valid_minimal_request_passes_required_fields() {
  const auto request = make_valid_request();
  const auto result = validate_recovery_request_required_fields(request);

  assert_true(result.ok,
              "valid RecoveryRequest must pass required nested evidence guard");
}

void test_valid_request_passes_boundary_guard() {
  const auto request = make_valid_request();
  const auto result = validate_recovery_request_boundary(request);

  assert_true(result.ok,
              "valid RecoveryRequest must pass boundary guard");
}

void test_missing_reflection_decision_rejected() {
  auto request = make_valid_request();
  request.reflection_decision = std::nullopt;

  const auto result = validate_recovery_request_required_fields(request);
  assert_true(!result.ok, "missing reflection_decision must be rejected");
  assert_equal("reflection_decision is required",
               std::string(result.reason),
               "missing reflection_decision must return canonical reason");
}

void test_success_observation_rejected() {
  auto request = make_valid_request();
  request.latest_observation->success = true;
  request.latest_observation->error = std::nullopt;

  const auto result = validate_recovery_request_boundary(request);
  assert_true(!result.ok, "success observation must be rejected");
  assert_equal("latest_observation must describe a failed execution",
               std::string(result.reason),
               "success observation must return canonical boundary reason");
}

void test_succeeded_checkpoint_rejected() {
  auto request = make_valid_request();
  request.checkpoint->state = CheckpointState::Succeeded;

  const auto result = validate_recovery_request_boundary(request);
  assert_true(!result.ok, "succeeded checkpoint must be rejected");
  assert_equal("checkpoint.state must not be Succeeded for recovery admission",
               std::string(result.reason),
               "succeeded checkpoint must return canonical boundary reason");
}

void test_retry_count_mismatch_rejected() {
  auto request = make_valid_request();
  request.retry_count = 2U;

  const auto result = validate_recovery_request_boundary(request);
  assert_true(!result.ok, "retry_count mismatch must be rejected");
  assert_equal("retry_count must match checkpoint.retry_count when both are present",
               std::string(result.reason),
               "retry_count mismatch must return canonical boundary reason");
}

void test_non_replayable_reason_required_when_replay_is_unsafe() {
  auto request = make_valid_request();
  request.idempotency_and_side_effect_report->non_replayable_reason = std::nullopt;

  const auto result = validate_recovery_request_required_fields(request);
  assert_true(!result.ok,
              "missing non_replayable_reason must be rejected when replay_safe=false");
  assert_equal("non_replayable_reason is required when replay_safe is false",
               std::string(result.reason),
               "missing non_replayable_reason must return canonical reason");
}

void test_second_reflection_field_rejected() {
  const auto result =
      validate_recovery_request_contract_field_boundary("decision_kind");

  assert_true(!result.allowed,
              "decision_kind must be rejected at RecoveryRequest top level");
  assert_equal(
      static_cast<int>(
          RecoveryRequestBoundaryDecision::RejectReflectionTopLevelField),
      static_cast<int>(result.decision),
      "reflection top-level intrusion must preserve rejection decision code");
}

void test_outcome_field_rejected() {
  const auto result =
      validate_recovery_request_contract_field_boundary("executed_action");

  assert_true(!result.allowed,
              "executed_action must be rejected for RecoveryRequest");
  assert_equal(static_cast<int>(RecoveryRequestBoundaryDecision::RejectOutcomeField),
               static_cast<int>(result.decision),
               "outcome-field intrusion must preserve rejection decision code");
}

}  // namespace

int main() {
  try {
    test_valid_minimal_request_passes_required_fields();
    test_valid_request_passes_boundary_guard();

    test_missing_reflection_decision_rejected();
    test_success_observation_rejected();
    test_succeeded_checkpoint_rejected();
    test_retry_count_mismatch_rejected();
    test_non_replayable_reason_required_when_replay_is_unsafe();
    test_second_reflection_field_rejected();
    test_outcome_field_rejected();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}