#include "RecoveryManager.h"

#include <optional>
#include <string>

#include "checkpoint/RecoveryOutcomeGuards.h"
#include "checkpoint/RecoveryRequestGuards.h"

namespace dasall::runtime {
namespace {

[[nodiscard]] const contracts::BudgetSnapshotEntry* find_budget_entry(
    const contracts::BudgetSnapshot& snapshot,
    const contracts::BudgetType budget_type) {
  for (const auto& entry : snapshot.entries) {
    if (entry.budget_type == budget_type) {
      return &entry;
    }
  }

  return nullptr;
}

[[nodiscard]] bool snapshot_is_exhausted(const contracts::BudgetSnapshot& snapshot) {
  if (snapshot.overall_reject_reason.has_value() && !snapshot.overall_reject_reason->empty()) {
    return true;
  }

  for (const auto& entry : snapshot.entries) {
    if (entry.current > entry.max || entry.reject_reason.has_value()) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] bool replan_budget_available(const contracts::BudgetSnapshot& snapshot) {
  const auto* entry = find_budget_entry(snapshot, contracts::BudgetType::Replan);
  if (entry == nullptr) {
    return true;
  }

  return entry->current <= entry->max && !entry->reject_reason.has_value();
}

[[nodiscard]] const char* runtime_error_code_label(const RuntimeErrorCode code) {
  switch (code) {
    case RuntimeErrorCode::RT_E_304_REPLAN_OVERRUN:
      return "RT_E_304_REPLAN_OVERRUN";
    case RuntimeErrorCode::RT_E_412_RESUME_REJECTED:
      return "RT_E_412_RESUME_REJECTED";
    case RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED:
      return "RT_E_500_RECOVERY_REJECTED";
    case RuntimeErrorCode::RT_E_510_SAFE_MODE_ENTERED:
      return "RT_E_510_SAFE_MODE_ENTERED";
    case RuntimeErrorCode::RT_E_511_DEGRADE_ENTERED:
      return "RT_E_511_DEGRADE_ENTERED";
    default:
      return "RT_E_000_UNSPECIFIED";
  }
}

[[nodiscard]] std::string make_detail(
    const RuntimeErrorCode code,
    const std::string& detail) {
  return std::string(runtime_error_code_label(code)) + ": " + detail;
}

[[nodiscard]] RecoveryExecutionPlan make_degrade_plan(const std::string& detail) {
  return make_recovery_escalation_plan(
      detail,
      SafeFailureHint{
          .enter_safe_mode = false,
          .enter_degraded_mode = true,
          .reason = detail,
      });
}

[[nodiscard]] std::string make_replan_token(const contracts::RecoveryRequest& request) {
  const auto checkpoint_ref = request.checkpoint->checkpoint_id.value_or(std::string("unknown"));
  const auto retry_count = request.retry_count.value_or(request.checkpoint->retry_count.value_or(0U));
  return std::string("replan:") + checkpoint_ref + ":" + std::to_string(retry_count + 1U);
}

}  // namespace

RecoveryExecutionPlan RecoveryManager::evaluate(
    const contracts::RecoveryRequest& request) const {
  const auto request_guard = contracts::validate_recovery_request_field_rules(request);
  if (!request_guard.ok) {
    return make_recovery_rejection_plan(
        make_detail(
            RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED,
            std::string(request_guard.reason)),
        RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED);
  }

  if (request.runtime_budget_snapshot.has_value() &&
      snapshot_is_exhausted(*request.runtime_budget_snapshot)) {
    const auto reason = request.runtime_budget_snapshot->overall_reject_reason.value_or(
        std::string("runtime budget exhausted during recovery admission"));
    return make_degrade_plan(reason);
  }

  const auto retry_count = request.retry_count.value_or(request.checkpoint->retry_count.value_or(0U));
  {
    const std::lock_guard<std::mutex> lock(recovery_mutex_);
    last_evaluated_retry_count_ = retry_count + 1U;
  }

  switch (*request.reflection_decision->decision_kind) {
    case contracts::ReflectionDecisionKind::AbortSafe:
      return make_recovery_escalation_plan(
          "reflection requested abort_safe escalation",
          SafeFailureHint{
              .enter_safe_mode = true,
              .enter_degraded_mode = false,
              .reason = "abort_safe should enter a supervised recovery path",
          });

    case contracts::ReflectionDecisionKind::Replan: {
      if (request.runtime_budget_snapshot.has_value() &&
          !replan_budget_available(*request.runtime_budget_snapshot)) {
        return make_recovery_rejection_plan(
            make_detail(
                RuntimeErrorCode::RT_E_304_REPLAN_OVERRUN,
                "replan budget exhausted"),
            RuntimeErrorCode::RT_E_304_REPLAN_OVERRUN);
      }

      const auto replan_token = make_replan_token(request);
      return make_recovery_execution_plan(
          "replan",
          "replan admitted; new retry_idempotency_token=" + replan_token,
          std::nullopt);
    }

    case contracts::ReflectionDecisionKind::Continue:
    case contracts::ReflectionDecisionKind::RetryStep: {
      const auto& report = *request.idempotency_and_side_effect_report;
      if (!report.replay_safe.value()) {
        return make_recovery_rejection_plan(
            make_detail(
                RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED,
                report.non_replayable_reason.value_or(
                    std::string("replay rejected because side effects are not replay-safe"))),
            RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED);
      }

      if (!report.idempotency_key.has_value() || report.idempotency_key->empty()) {
        return make_recovery_rejection_plan(
            make_detail(
                RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED,
                "retry_step requires retry_idempotency_token reuse evidence"),
            RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED);
      }

      const auto resume_decision = checkpoint_manager_.make_resume_plan(*request.checkpoint);
      if (resume_decision.rejected()) {
        return make_recovery_rejection_plan(
            make_detail(
                resume_decision.error_code.value_or(RuntimeErrorCode::RT_E_412_RESUME_REJECTED),
                resume_decision.detail),
            resume_decision.error_code.value_or(RuntimeErrorCode::RT_E_412_RESUME_REJECTED));
      }

      const auto action = *request.reflection_decision->decision_kind ==
                                  contracts::ReflectionDecisionKind::Continue
                              ? std::string("continue")
                              : std::string("retry_step");
      return make_recovery_execution_plan(
          action,
          action + " admitted; reuse retry_idempotency_token=" + *report.idempotency_key,
          resume_decision.plan);
    }

    case contracts::ReflectionDecisionKind::Unspecified:
      return make_recovery_rejection_plan(
          make_detail(
              RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED,
              "reflection decision must be concrete"),
          RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED);
  }

  return make_recovery_rejection_plan(
      make_detail(
          RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED,
          "recovery decision is not supported"),
      RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED);
}

contracts::RecoveryOutcome RecoveryManager::execute(
    const RecoveryExecutionPlan& plan) {
  std::optional<std::uint32_t> updated_retry_count;
  {
    const std::lock_guard<std::mutex> lock(recovery_mutex_);
    updated_retry_count = last_evaluated_retry_count_;
  }

  if (!plan.executable()) {
    if (plan.escalated()) {
      const bool degraded = plan.safe_failure_hint.has_value() &&
                            plan.safe_failure_hint->enter_degraded_mode;
      return contracts::RecoveryOutcome{
          .executed_action = degraded ? std::optional<std::string>(std::string("degrade"))
                                      : std::optional<std::string>(std::string("abort_safe")),
          .final_runtime_state = degraded ? std::optional<std::string>(std::string("Degraded"))
                                          : std::optional<std::string>(std::string("FailedSafe")),
          .updated_retry_count = updated_retry_count,
          .checkpoint_ref = std::nullopt,
          .compensation_result_ref = std::nullopt,
          .rejection_reason = std::nullopt,
          .escalation_reason = plan.detail,
      };
    }

    return contracts::RecoveryOutcome{
        .executed_action = std::string("rejected"),
        .final_runtime_state = std::string("FailedSafe"),
        .updated_retry_count = updated_retry_count,
        .checkpoint_ref = std::nullopt,
        .compensation_result_ref = std::nullopt,
        .rejection_reason = plan.detail,
        .escalation_reason = std::nullopt,
    };
  }

  const auto final_state = plan.planned_action == "replan"
                               ? std::string("Planning")
                               : plan.resume_plan.has_value()
                                     ? std::string(runtime_state_name(plan.resume_plan->target_state))
                                     : std::string("Reasoning");

  return contracts::RecoveryOutcome{
      .executed_action = plan.planned_action,
      .final_runtime_state = final_state,
      .updated_retry_count = updated_retry_count,
      .checkpoint_ref = plan.resume_plan.has_value()
                ? std::optional<std::string>(plan.resume_plan->checkpoint_ref)
                : std::nullopt,
      .compensation_result_ref = std::nullopt,
      .rejection_reason = std::nullopt,
      .escalation_reason = std::nullopt,
  };
}

RecoveryApplyResult RecoveryManager::apply(
    const contracts::RecoveryOutcome& outcome) {
  const auto guard = contracts::validate_recovery_outcome_field_rules(outcome);
  if (!guard.ok) {
    return RecoveryApplyResult{
        .applied = false,
        .error_code = RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED,
        .detail = std::string(guard.reason),
    };
  }

  {
    const std::lock_guard<std::mutex> lock(recovery_mutex_);
    last_outcome_ = outcome;
  }

  if (*outcome.executed_action == "rejected") {
    return RecoveryApplyResult{
        .applied = false,
        .error_code = RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED,
        .detail = outcome.rejection_reason.value_or(
            make_detail(
                RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED,
                "rejected outcomes are not applied")),
    };
  }

  if (*outcome.executed_action == "abort_safe") {
    return RecoveryApplyResult{
        .applied = true,
        .error_code = RuntimeErrorCode::RT_E_510_SAFE_MODE_ENTERED,
        .detail = make_detail(
            RuntimeErrorCode::RT_E_510_SAFE_MODE_ENTERED,
            "abort_safe outcome applied"),
    };
  }

  if (*outcome.executed_action == "degrade") {
    return RecoveryApplyResult{
        .applied = true,
        .error_code = RuntimeErrorCode::RT_E_511_DEGRADE_ENTERED,
        .detail = make_detail(
            RuntimeErrorCode::RT_E_511_DEGRADE_ENTERED,
            "degraded recovery outcome applied"),
    };
  }

  return RecoveryApplyResult{
      .applied = true,
      .error_code = std::nullopt,
      .detail = "recovery outcome applied",
  };
}

const std::optional<contracts::RecoveryOutcome>& RecoveryManager::last_outcome() const {
  return last_outcome_;
}

}  // namespace dasall::runtime