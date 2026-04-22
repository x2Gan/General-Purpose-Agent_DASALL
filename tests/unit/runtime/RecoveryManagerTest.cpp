#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include "recovery/IRecoveryManager.h"
#include "support/TestAssertions.h"

namespace {

class FakeRecoveryManager final : public dasall::runtime::IRecoveryManager {
 public:
  [[nodiscard]] dasall::runtime::RecoveryExecutionPlan evaluate(
      const dasall::contracts::RecoveryRequest& request) const override {
    using dasall::contracts::CheckpointState;
    using dasall::contracts::ReflectionDecisionKind;
    using dasall::runtime::ResumePlan;
    using dasall::runtime::RuntimeErrorCode;
    using dasall::runtime::RuntimeState;
    using dasall::runtime::SafeFailureHint;
    using dasall::runtime::make_recovery_escalation_plan;
    using dasall::runtime::make_recovery_execution_plan;
    using dasall::runtime::make_recovery_rejection_plan;
    using dasall::runtime::resume_target_state;

    if (!request.checkpoint.has_value() || !request.checkpoint->checkpoint_id.has_value()) {
      return make_recovery_rejection_plan(
          "recovery evaluation requires checkpoint_ref",
          RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED);
    }

    if (!request.idempotency_and_side_effect_report.has_value() ||
        !request.idempotency_and_side_effect_report->replay_safe.has_value()) {
      return make_recovery_rejection_plan(
          "recovery evaluation requires replay-safety evidence",
          RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED);
    }

    if (!request.idempotency_and_side_effect_report->replay_safe.value()) {
      return make_recovery_rejection_plan(
          request.idempotency_and_side_effect_report->non_replayable_reason.value_or(
              std::string("replay rejected because side effects are not replay-safe")),
          RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED);
    }

    if (request.reflection_decision.has_value() &&
        request.reflection_decision->decision_kind == ReflectionDecisionKind::AbortSafe) {
      return make_recovery_escalation_plan(
          "reflection requested abort_safe escalation",
          SafeFailureHint{
              .enter_safe_mode = true,
              .enter_degraded_mode = false,
              .reason = "abort_safe should enter a supervised recovery path",
          });
    }

    const auto checkpoint_state = request.checkpoint->state.value_or(CheckpointState::Running);
    const auto target_state = resume_target_state(checkpoint_state).value_or(RuntimeState::Reasoning);
    return make_recovery_execution_plan(
        "retry_step",
        "replay-safe retry admitted by fake recovery manager",
        ResumePlan{
            .checkpoint_ref = *request.checkpoint->checkpoint_id,
            .target_state = target_state,
            .checkpoint_state = checkpoint_state,
            .resume_reason = "retry last failed step",
            .pending_action = request.checkpoint->pending_action,
            .policy_snapshot_ref = std::nullopt,
            .requires_operator_intervention = false,
        });
  }

  [[nodiscard]] dasall::contracts::RecoveryOutcome execute(
      const dasall::runtime::RecoveryExecutionPlan& plan) override {
    if (!plan.executable()) {
      return dasall::contracts::RecoveryOutcome{
          .executed_action = std::string("rejected"),
          .final_runtime_state = std::string("FailedSafe"),
          .updated_retry_count = std::nullopt,
          .checkpoint_ref = std::nullopt,
          .compensation_result_ref = std::nullopt,
          .rejection_reason = plan.detail,
          .escalation_reason = std::nullopt,
      };
    }

    return dasall::contracts::RecoveryOutcome{
        .executed_action = plan.planned_action,
        .final_runtime_state = plan.resume_plan.has_value()
                                   ? std::optional<std::string>(std::string(
                                         dasall::runtime::runtime_state_name(
                                             plan.resume_plan->target_state)))
                                   : std::optional<std::string>(std::string("Reasoning")),
        .updated_retry_count = 2,
        .checkpoint_ref = plan.resume_plan.has_value()
                              ? std::optional<std::string>(plan.resume_plan->checkpoint_ref)
                              : std::nullopt,
        .compensation_result_ref = std::nullopt,
        .rejection_reason = std::nullopt,
        .escalation_reason = plan.escalated() ? std::optional<std::string>(plan.detail)
                                              : std::nullopt,
    };
  }

  [[nodiscard]] dasall::runtime::RecoveryApplyResult apply(
      const dasall::contracts::RecoveryOutcome& outcome) override {
    if (!outcome.executed_action.has_value()) {
      return dasall::runtime::RecoveryApplyResult{
          .applied = false,
          .error_code = dasall::runtime::RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED,
          .detail = "recovery outcome is missing executed_action",
      };
    }

    last_outcome_ = outcome;
    return dasall::runtime::RecoveryApplyResult{
        .applied = outcome.executed_action.value() != "rejected",
        .error_code = outcome.executed_action.value() == "rejected"
                          ? std::optional<dasall::runtime::RuntimeErrorCode>(
                                dasall::runtime::RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED)
                          : std::nullopt,
        .detail = outcome.executed_action.value() == "rejected"
                      ? "rejected outcomes are not applied"
                      : "recovery outcome applied to fake runtime state",
    };
  }

  [[nodiscard]] const std::optional<dasall::contracts::RecoveryOutcome>& last_outcome() const {
    return last_outcome_;
  }

 private:
  std::optional<dasall::contracts::RecoveryOutcome> last_outcome_;
};

[[nodiscard]] dasall::contracts::RecoveryRequest make_recovery_request(
    const bool replay_safe,
    const dasall::contracts::ReflectionDecisionKind decision_kind =
        dasall::contracts::ReflectionDecisionKind::RetryStep) {
  const dasall::contracts::ErrorInfo error_info{
      .failure_type = dasall::contracts::ResultCodeCategory::Runtime,
      .retryable = true,
      .safe_to_replan = true,
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
          .tags = std::vector<std::string>{"rt.schema_version=1", "rt.fsm_state_enum_version=1"},
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
      .runtime_budget_snapshot = std::nullopt,
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
    FakeRecoveryManager manager;

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

    const auto rejected_plan = manager.evaluate(make_recovery_request(false));
    assert_true(!rejected_plan.executable(), "unsafe replay should be rejected");
    assert_true(rejected_plan.admission == RecoveryAdmission::Reject,
                "unsafe replay should produce Reject admission state");
    assert_true(rejected_plan.error_code == RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED,
                "unsafe replay should map to RT_E_500_RECOVERY_REJECTED");

    const auto escalated_plan = manager.evaluate(
        make_recovery_request(true, ReflectionDecisionKind::AbortSafe));
    assert_true(escalated_plan.escalated(),
                "abort_safe reflection should escalate rather than admit retry execution");
    assert_true(escalated_plan.safe_failure_hint.has_value(),
                "escalation path should expose safe failure hint");
    assert_true(escalated_plan.safe_failure_hint->enter_safe_mode,
                "abort_safe escalation should request safe mode entry");
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}