#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "AgentTypes.h"
#include "checkpoint/Checkpoint.h"
#include "checkpoint/RecoveryRequest.h"
#include "error/ErrorInfo.h"
#include "observation/Observation.h"
#include "recovery/RecoveryManager.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::CheckpointState;
using dasall::contracts::ObservationSource;
using dasall::contracts::ReflectionDecisionKind;
using dasall::runtime::RecoveryAdmission;
using dasall::runtime::RecoveryManager;
using dasall::runtime::RuntimeErrorCode;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::contracts::BudgetSnapshot make_budget_snapshot(
    const bool exhausted) {
  using dasall::contracts::BudgetSnapshot;
  using dasall::contracts::BudgetSnapshotEntry;
  using dasall::contracts::BudgetType;

  return BudgetSnapshot{
      .snapshot_at_ms = 1710000172000,
      .entries = {
          BudgetSnapshotEntry{
              .budget_type = BudgetType::Token,
              .current = 128,
              .max = 2048,
              .remaining = 1920,
              .reject_reason = std::nullopt,
          },
          BudgetSnapshotEntry{
              .budget_type = BudgetType::Turn,
              .current = 1,
              .max = 6,
              .remaining = 5,
              .reject_reason = std::nullopt,
          },
          BudgetSnapshotEntry{
              .budget_type = BudgetType::ToolCall,
              .current = exhausted ? 2ULL : 1ULL,
              .max = 1,
              .remaining = exhausted ? -1 : 0,
              .reject_reason = exhausted
                                   ? std::optional<std::string>(
                                         "runtime-local tool-call budget exhausted")
                                   : std::nullopt,
          },
          BudgetSnapshotEntry{
              .budget_type = BudgetType::Latency,
              .current = 250,
              .max = 4000,
              .remaining = 3750,
              .reject_reason = std::nullopt,
          },
          BudgetSnapshotEntry{
              .budget_type = BudgetType::Replan,
              .current = 0,
              .max = 2,
              .remaining = 2,
              .reject_reason = std::nullopt,
          },
      },
      .overall_reject_reason = exhausted
                                   ? std::optional<std::string>(
                                         "runtime-local tool-call budget exhausted")
                                   : std::nullopt,
  };
}

[[nodiscard]] dasall::contracts::RecoveryRequest make_recovery_request(
    const std::string& idempotency_key,
    const bool exhausted_budget) {
  const auto checkpoint_id = std::string{"chk-017-recovery"};
  const auto error_info = dasall::contracts::ErrorInfo{
      .failure_type = dasall::contracts::ResultCodeCategory::Runtime,
      .retryable = true,
      .safe_to_replan = false,
      .details = dasall::contracts::ErrorDetails{
          .code = 5001,
          .message = "tool round failed and needs runtime-owned recovery",
          .stage = "tool_round",
      },
      .source_ref = dasall::contracts::ErrorSourceRefMinimal{
          .ref_type = "tool_call",
          .ref_id = "tool-call-req-017-recovery",
      },
  };

  return dasall::contracts::RecoveryRequest{
      .reflection_decision = dasall::contracts::ReflectionDecision{
          .request_id = std::string{"req-017-recovery"},
          .decision_kind = ReflectionDecisionKind::RetryStep,
          .rationale = std::string{"runtime should reuse runtime-owned retry evidence"},
          .goal_id = std::string{"goal-017-recovery"},
          .confidence = 0.92F,
          .relevant_observation_refs = std::vector<std::string>{"obs-017-recovery"},
          .hint_ref = std::string{"hint-017-recovery"},
          .created_at = 1710000172001,
          .tags = std::vector<std::string>{"integration", "runtime_recovery_context"},
      },
      .error_info = error_info,
      .latest_observation = dasall::contracts::Observation{
          .observation_id = std::string{"obs-017-recovery"},
          .source = ObservationSource::ToolExecution,
          .success = false,
          .payload = std::string{"{}"},
          .created_at = 1710000172002,
          .error = error_info,
          .side_effects = std::nullopt,
          .tool_call_id = std::string{"tool-call-req-017-recovery"},
          .worker_task_id = std::nullopt,
          .request_id = std::string{"req-017-recovery"},
          .goal_id = std::string{"goal-017-recovery"},
          .duration_ms = 18,
          .tags = std::vector<std::string>{"integration", "runtime_recovery_context"},
      },
      .checkpoint = dasall::contracts::Checkpoint{
          .checkpoint_id = checkpoint_id,
          .state = CheckpointState::Running,
          .step_id = std::string{"tool-call"},
          .working_memory_snapshot = std::string{"wm:017:recovery"},
          .pending_action = std::string{},
          .request_id = std::string{"req-017-recovery"},
          .goal_id = std::string{"goal-017-recovery"},
          .belief_state_ref = std::string{"belief-017-recovery"},
          .retry_count = 1,
          .created_at = 1710000172003,
          .tags = std::vector<std::string>{
              "rt.schema_version=1",
              "rt.fsm_state_enum_version=1",
              "rt.budget_schema_version=1",
          },
      },
      .idempotency_and_side_effect_report =
          dasall::contracts::IdempotencyAndSideEffectReport{
              .replay_safe = true,
              .idempotency_key = idempotency_key,
              .side_effects_present = false,
              .non_replayable_reason = std::nullopt,
          },
      .retry_count = 1,
      .runtime_budget_snapshot = make_budget_snapshot(exhausted_budget),
  };
}

void test_runtime_recovery_context_integration_reuses_resume_binding_token() {
  RecoveryManager manager;
  const auto retry_token =
      dasall::runtime::make_resume_binding_token("session-017-recovery", "chk-017-recovery");
  const auto plan = manager.evaluate(make_recovery_request(retry_token, false));

  assert_true(plan.admission == RecoveryAdmission::Admit && plan.executable(),
              "retry_step with runtime-owned resume binding token should be admitted");
  assert_true(plan.resume_plan.has_value(),
              "admitted retry_step should materialize a concrete resume plan");
  assert_true(plan.detail.find(retry_token) != std::string::npos,
              "recovery admission detail should reuse the runtime-owned binding token verbatim");

  const auto outcome = manager.execute(plan);
  assert_true(outcome.executed_action == std::string("retry_step") &&
                  outcome.final_runtime_state == std::string("Reasoning"),
              "retry_step should keep runtime on the resumable reasoning path");

  const auto apply_result = manager.apply(outcome);
  assert_true(apply_result.applied && !apply_result.error_code.has_value(),
              "admitted retry_step outcome should apply without introducing a new runtime error code");
}

void test_runtime_recovery_context_integration_consumes_budget_snapshot() {
  RecoveryManager manager;
  const auto retry_token =
      dasall::runtime::make_resume_binding_token("session-017-recovery", "chk-017-recovery");
  const auto plan = manager.evaluate(make_recovery_request(retry_token, true));

  assert_true(plan.escalated() && plan.safe_failure_hint.has_value() &&
                  plan.safe_failure_hint->enter_degraded_mode,
              "exhausted runtime budget snapshot should escalate into degraded recovery");
  assert_true(plan.detail.find("runtime-local tool-call budget exhausted") != std::string::npos,
              "budget exhaustion detail should stay in runtime-owned recovery evidence");

  const auto outcome = manager.execute(plan);
  assert_true(outcome.executed_action == std::string("degrade") &&
                  outcome.final_runtime_state == std::string("Degraded"),
              "budget exhaustion should execute the degrade recovery action");

  const auto apply_result = manager.apply(outcome);
  assert_true(apply_result.applied &&
                  apply_result.error_code == RuntimeErrorCode::RT_E_511_DEGRADE_ENTERED,
              "degrade outcome should apply with RT_E_511_DEGRADE_ENTERED");
}

}  // namespace

int main() {
  try {
    test_runtime_recovery_context_integration_reuses_resume_binding_token();
    test_runtime_recovery_context_integration_consumes_budget_snapshot();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}