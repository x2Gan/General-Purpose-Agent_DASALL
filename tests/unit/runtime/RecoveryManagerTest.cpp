#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include "recovery/RecoveryManager.h"
#include "recovery/IRecoveryManager.h"
#include "support/TestAssertions.h"

namespace {
[[nodiscard]] dasall::contracts::RecoveryRequest make_recovery_request(
    const bool replay_safe,
    const dasall::contracts::ReflectionDecisionKind decision_kind =
        dasall::contracts::ReflectionDecisionKind::RetryStep,
    const bool safe_to_replan = true,
    const bool budget_exhausted = false) {
  const dasall::contracts::ErrorInfo error_info{
      .failure_type = dasall::contracts::ResultCodeCategory::Runtime,
      .retryable = true,
      .safe_to_replan = safe_to_replan,
      .details = {
          .code = 5001,
          .message = "tool execution failed",
          .stage = "tool_execution",
      },
      .source_ref = {
          .ref_type = "tool_call",
          .ref_id = "tool-42",
      },
  };

  return dasall::contracts::RecoveryRequest{
      .reflection_decision = dasall::contracts::ReflectionDecision{
          .request_id = std::string("req-200"),
          .decision_kind = decision_kind,
          .rationale = std::string("reflection recommends retrying the failed step"),
          .goal_id = std::string("goal-200"),
          .confidence = 0.8F,
          .relevant_observation_refs = std::vector<std::string>{"obs-200"},
          .hint_ref = std::string("hint-200"),
          .created_at = 1700000200,
          .tags = std::vector<std::string>{"unit=recovery"},
      },
      .error_info = error_info,
      .latest_observation = dasall::contracts::Observation{
          .observation_id = std::string("obs-200"),
          .source = dasall::contracts::ObservationSource::ToolExecution,
          .success = false,
          .payload = std::string("{}"),
          .created_at = 1700000201,
          .error = error_info,
          .side_effects = std::nullopt,
          .tool_call_id = std::string("tool-42"),
          .worker_task_id = std::nullopt,
          .request_id = std::string("req-200"),
          .goal_id = std::string("goal-200"),
          .duration_ms = 81,
          .tags = std::vector<std::string>{"unit=recovery"},
      },
      .checkpoint = dasall::contracts::Checkpoint{
          .checkpoint_id = std::string("chk-200"),
          .state = dasall::contracts::CheckpointState::Running,
          .step_id = std::string("tool-call"),
          .working_memory_snapshot = std::string("wm:recovery:1"),
          .pending_action = std::string(),
          .request_id = std::string("req-200"),
          .goal_id = std::string("goal-200"),
          .belief_state_ref = std::string("belief-200"),
          .retry_count = 1,
          .created_at = 1700000202,
          .tags = std::vector<std::string>{
            "rt.schema_version=1",
            "rt.fsm_state_enum_version=1",
            "rt.budget_schema_version=1",
          },
      },
      .idempotency_and_side_effect_report =
          dasall::contracts::IdempotencyAndSideEffectReport{
              .replay_safe = replay_safe,
              .idempotency_key = std::string("idem-200"),
              .side_effects_present = false,
              .non_replayable_reason = replay_safe
                                           ? std::nullopt
                                           : std::optional<std::string>(
                                                 "irreversible side effect already observed"),
          },
      .retry_count = 1,
        .runtime_budget_snapshot = budget_exhausted
                       ? std::optional<dasall::contracts::BudgetSnapshot>(
                           dasall::contracts::BudgetSnapshot{
                             .snapshot_at_ms = 1700000203,
                             .entries = {
                               {.budget_type = dasall::contracts::BudgetType::Replan,
                              .current = 2,
                              .max = 1,
                              .remaining = -1,
                              .reject_reason = std::string("replan exhausted")},
                             },
                             .overall_reject_reason = std::string("budget exhausted"),
                           })
                       : std::nullopt,
  };
}

}  // namespace

int main() {
  using dasall::contracts::ReflectionDecisionKind;
  using dasall::runtime::RecoveryAdmission;
  using dasall::runtime::RuntimeErrorCode;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  try {
    dasall::runtime::RecoveryManager manager;

    const auto admitted_plan = manager.evaluate(make_recovery_request(true));
    assert_true(admitted_plan.executable(), "replay-safe recovery request should be admitted");
    assert_true(admitted_plan.admission == RecoveryAdmission::Admit,
                "admitted plan should keep Admit admission state");
    assert_equal("retry_step", admitted_plan.planned_action,
                 "admitted plan should select retry_step action");
    assert_true(admitted_plan.resume_plan.has_value(),
                "admitted plan should carry a concrete resume plan");

    const auto admitted_outcome = manager.execute(admitted_plan);
    assert_equal("retry_step", admitted_outcome.executed_action.value_or(std::string()),
                 "execute should keep retry_step as executed action");
    assert_equal("Reasoning", admitted_outcome.final_runtime_state.value_or(std::string()),
                 "retry-step execution should re-enter Reasoning state");
    assert_equal("chk-200", admitted_outcome.checkpoint_ref.value_or(std::string()),
                 "recovery outcome should keep checkpoint reference");

    const auto apply_result = manager.apply(admitted_outcome);
    assert_true(apply_result.applied, "admitted recovery outcome should apply successfully");
    assert_true(manager.last_outcome().has_value(), "apply should retain last recovery outcome");

    const auto replan_plan = manager.evaluate(
      make_recovery_request(true, ReflectionDecisionKind::Replan));
    assert_true(replan_plan.executable(), "replan reflection should be admitted when budget allows");
    assert_equal("replan", replan_plan.planned_action,
           "replan reflection should select replan action");
    assert_true(replan_plan.detail.find("new retry_idempotency_token=replan:chk-200:2") !=
            std::string::npos,
          "replan admission should generate a new retry idempotency token");

    const auto replan_outcome = manager.execute(replan_plan);
    assert_equal("Planning", replan_outcome.final_runtime_state.value_or(std::string()),
           "replan execution should move runtime into Planning state");

    const auto rejected_plan = manager.evaluate(make_recovery_request(false, ReflectionDecisionKind::RetryStep, false));
    assert_true(!rejected_plan.executable(), "unsafe replay should be rejected");
    assert_true(rejected_plan.admission == RecoveryAdmission::Reject,
                "unsafe replay should produce Reject admission state");
    assert_true(rejected_plan.error_code == RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED,
                "unsafe replay should map to RT_E_500_RECOVERY_REJECTED");

    const auto degraded_plan = manager.evaluate(
      make_recovery_request(true, ReflectionDecisionKind::RetryStep, true, true));
    assert_true(degraded_plan.escalated(),
          "budget exhaustion should escalate into degraded recovery path");
    assert_true(degraded_plan.safe_failure_hint.has_value() &&
            degraded_plan.safe_failure_hint->enter_degraded_mode,
          "budget exhaustion should request degraded mode entry");

    const auto degraded_outcome = manager.execute(degraded_plan);
    assert_equal("degrade", degraded_outcome.executed_action.value_or(std::string()),
           "degraded escalation should execute degrade action");
    const auto degraded_apply = manager.apply(degraded_outcome);
    assert_true(degraded_apply.applied &&
            degraded_apply.error_code == RuntimeErrorCode::RT_E_511_DEGRADE_ENTERED,
          "degrade outcome should apply and expose RT_E_511_DEGRADE_ENTERED");

    const auto escalated_plan = manager.evaluate(
        make_recovery_request(true, ReflectionDecisionKind::AbortSafe));
    assert_true(escalated_plan.escalated(),
                "abort_safe reflection should escalate rather than admit retry execution");
    assert_true(escalated_plan.safe_failure_hint.has_value(),
                "escalation path should expose safe failure hint");
    assert_true(escalated_plan.safe_failure_hint->enter_safe_mode,
                "abort_safe escalation should request safe mode entry");

    const auto escalated_outcome = manager.execute(escalated_plan);
    assert_equal("abort_safe", escalated_outcome.executed_action.value_or(std::string()),
           "abort_safe escalation should execute abort_safe action");
    const auto escalated_apply = manager.apply(escalated_outcome);
    assert_true(escalated_apply.applied &&
            escalated_apply.error_code == RuntimeErrorCode::RT_E_510_SAFE_MODE_ENTERED,
          "abort_safe outcome should apply and expose RT_E_510_SAFE_MODE_ENTERED");
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}