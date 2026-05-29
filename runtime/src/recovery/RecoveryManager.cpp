#include "RecoveryManager.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "../logging/RuntimeStructuredLogUtils.h"
#include "checkpoint/RecoveryOutcomeGuards.h"
#include "checkpoint/RecoveryRequestGuards.h"

namespace dasall::runtime {
namespace {

constexpr std::string_view kRecoveryTokenMarker = "retry_idempotency_token=";

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

[[nodiscard]] const char* reflection_decision_kind_name(
    const std::optional<contracts::ReflectionDecisionKind>& decision_kind) {
  if (!decision_kind.has_value()) {
    return "Unknown";
  }

  switch (*decision_kind) {
    case contracts::ReflectionDecisionKind::Unspecified:
      return "Unspecified";
    case contracts::ReflectionDecisionKind::Continue:
      return "Continue";
    case contracts::ReflectionDecisionKind::RetryStep:
      return "RetryStep";
    case contracts::ReflectionDecisionKind::Replan:
      return "Replan";
    case contracts::ReflectionDecisionKind::AbortSafe:
      return "AbortSafe";
  }

  return "Unknown";
}

[[nodiscard]] std::optional<std::string> recovery_request_id(
    const contracts::RecoveryRequest& request) {
  if (request.reflection_decision.has_value() &&
      request.reflection_decision->request_id.has_value() &&
      !request.reflection_decision->request_id->empty()) {
    return request.reflection_decision->request_id;
  }

  if (request.latest_observation.has_value() && request.latest_observation->request_id.has_value() &&
      !request.latest_observation->request_id->empty()) {
    return request.latest_observation->request_id;
  }

  if (request.checkpoint.has_value() && request.checkpoint->request_id.has_value() &&
      !request.checkpoint->request_id->empty()) {
    return request.checkpoint->request_id;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> checkpoint_ref(
    const contracts::RecoveryRequest& request) {
  if (request.checkpoint.has_value() && request.checkpoint->checkpoint_id.has_value() &&
      !request.checkpoint->checkpoint_id->empty()) {
    return request.checkpoint->checkpoint_id;
  }

  return std::nullopt;
}

[[nodiscard]] bool is_detail_delimiter(const char ch) {
  switch (ch) {
    case ' ':
    case '\t':
    case '\n':
    case '\r':
    case ',':
    case ';':
    case ')':
    case '(':
    case ']':
    case '[':
    case '}':
    case '{':
      return true;
    default:
      return false;
  }
}

[[nodiscard]] std::string sanitize_recovery_detail(std::string detail) {
  std::size_t search_pos = 0U;
  while (search_pos < detail.size()) {
    const auto marker_pos = detail.find(kRecoveryTokenMarker, search_pos);
    if (marker_pos == std::string::npos) {
      break;
    }

    const auto value_start = marker_pos + kRecoveryTokenMarker.size();
    auto value_end = value_start;
    while (value_end < detail.size() && !is_detail_delimiter(detail[value_end])) {
      ++value_end;
    }

    detail.replace(value_start, value_end - value_start, std::string("<redacted>"));
    search_pos = value_start + std::string_view("<redacted>").size();
  }

  return detail;
}

[[nodiscard]] infra::LogLevel recovery_plan_log_level(
    const RecoveryExecutionPlan& plan) {
  if (plan.executable()) {
    return infra::LogLevel::Info;
  }

  if (plan.escalated()) {
    return infra::LogLevel::Warn;
  }

  return infra::LogLevel::Warn;
}

[[nodiscard]] infra::LogLevel recovery_outcome_log_level(
    const contracts::RecoveryOutcome& outcome) {
  if (outcome.executed_action == std::optional<std::string>(std::string("rejected"))) {
    return infra::LogLevel::Warn;
  }

  if (outcome.executed_action == std::optional<std::string>(std::string("abort_safe")) ||
      outcome.executed_action == std::optional<std::string>(std::string("degrade"))) {
    return infra::LogLevel::Warn;
  }

  return infra::LogLevel::Info;
}

[[nodiscard]] infra::LogLevel recovery_apply_log_level(
    const RecoveryApplyResult& result) {
  if (!result.applied) {
    return infra::LogLevel::Error;
  }

  if (result.error_code.has_value()) {
    return infra::LogLevel::Warn;
  }

  return infra::LogLevel::Info;
}

void emit_recovery_plan_log(
    const std::shared_ptr<infra::logging::ILogger>& logger,
    const std::optional<std::string>& runtime_instance_id,
    const contracts::RecoveryRequest& request,
    const RecoveryExecutionPlan& plan) {
  infra::LogEvent::AttributeMap attrs;
  detail::add_optional_string_attr(attrs, "request_id", recovery_request_id(request));
  detail::add_optional_string_attr(attrs, "checkpoint_ref", checkpoint_ref(request));
  detail::add_string_attr(
      attrs,
      "reflection_decision_kind",
      reflection_decision_kind_name(
          request.reflection_decision.has_value()
              ? request.reflection_decision->decision_kind
              : std::optional<contracts::ReflectionDecisionKind>{}));
  detail::add_integer_attr(
      attrs,
      "retry_count",
      request.retry_count.value_or(
          request.checkpoint.has_value()
              ? request.checkpoint->retry_count.value_or(0U)
              : 0U));
  detail::add_bool_attr(
      attrs,
      "has_runtime_budget_snapshot",
      request.runtime_budget_snapshot.has_value());
  detail::add_bool_attr(
      attrs,
      "replay_safe",
      request.idempotency_and_side_effect_report.has_value() &&
          request.idempotency_and_side_effect_report->replay_safe.value_or(false));
  detail::add_bool_attr(
      attrs,
      "side_effects_present",
      request.idempotency_and_side_effect_report.has_value() &&
          request.idempotency_and_side_effect_report->side_effects_present.value_or(false));
  detail::add_string_attr(attrs, "admission", recovery_admission_name(plan.admission));
  detail::add_string_attr(attrs, "planned_action", plan.planned_action);
  detail::add_bool_attr(attrs, "executable", plan.executable());
  detail::add_bool_attr(attrs, "escalated", plan.escalated());
  detail::add_bool_attr(attrs, "resume_plan_present", plan.resume_plan.has_value());
  if (plan.safe_failure_hint.has_value()) {
    detail::add_bool_attr(
        attrs,
        "enter_safe_mode",
        plan.safe_failure_hint->enter_safe_mode);
    detail::add_bool_attr(
        attrs,
        "enter_degraded_mode",
        plan.safe_failure_hint->enter_degraded_mode);
  }
  if (plan.error_code.has_value()) {
    detail::add_integer_attr(attrs, "error_code", static_cast<int>(*plan.error_code));
  }
  detail::add_string_attr(attrs, "detail", sanitize_recovery_detail(plan.detail));
  detail::emit_runtime_log(
      logger,
      recovery_plan_log_level(plan),
      "runtime.recovery.evaluate",
      "recovery_manager",
      runtime_instance_id,
      std::move(attrs));
}

void emit_recovery_outcome_log(
    const std::shared_ptr<infra::logging::ILogger>& logger,
    const std::optional<std::string>& runtime_instance_id,
    const contracts::RecoveryOutcome& outcome) {
  infra::LogEvent::AttributeMap attrs;
  detail::add_optional_string_attr(attrs, "checkpoint_ref", outcome.checkpoint_ref);
  detail::add_optional_string_attr(attrs, "executed_action", outcome.executed_action);
  detail::add_optional_string_attr(attrs, "final_runtime_state", outcome.final_runtime_state);
  detail::add_integer_attr(attrs,
                           "updated_retry_count",
                           outcome.updated_retry_count.value_or(0U));
  detail::add_bool_attr(
      attrs,
      "rejected",
      outcome.rejection_reason.has_value());
  detail::add_bool_attr(
      attrs,
      "escalated",
      outcome.escalation_reason.has_value());
  if (outcome.rejection_reason.has_value()) {
    detail::add_string_attr(
        attrs,
        "detail",
        sanitize_recovery_detail(*outcome.rejection_reason));
  } else if (outcome.escalation_reason.has_value()) {
    detail::add_string_attr(
        attrs,
        "detail",
        sanitize_recovery_detail(*outcome.escalation_reason));
  }
  detail::emit_runtime_log(
      logger,
      recovery_outcome_log_level(outcome),
      "runtime.recovery.execute",
      "recovery_manager",
      runtime_instance_id,
      std::move(attrs));
}

void emit_recovery_apply_log(
    const std::shared_ptr<infra::logging::ILogger>& logger,
    const std::optional<std::string>& runtime_instance_id,
    const contracts::RecoveryOutcome& outcome,
    const RecoveryApplyResult& result) {
  infra::LogEvent::AttributeMap attrs;
  detail::add_optional_string_attr(attrs, "checkpoint_ref", outcome.checkpoint_ref);
  detail::add_optional_string_attr(attrs, "executed_action", outcome.executed_action);
  detail::add_optional_string_attr(attrs, "final_runtime_state", outcome.final_runtime_state);
  detail::add_bool_attr(attrs, "applied", result.applied);
  detail::add_integer_attr(attrs,
                           "updated_retry_count",
                           outcome.updated_retry_count.value_or(0U));
  if (result.error_code.has_value()) {
    detail::add_integer_attr(attrs, "error_code", static_cast<int>(*result.error_code));
  }
  detail::add_string_attr(attrs, "detail", sanitize_recovery_detail(result.detail));
  detail::emit_runtime_log(
      logger,
      recovery_apply_log_level(result),
      "runtime.recovery.apply",
      "recovery_manager",
      runtime_instance_id,
      std::move(attrs));
}

}  // namespace

void RecoveryManager::set_logger(
    std::shared_ptr<infra::logging::ILogger> logger,
    std::optional<std::string> runtime_instance_id) {
  logger_ = std::move(logger);
  runtime_instance_id_ = std::move(runtime_instance_id);
  checkpoint_manager_.set_logger(logger_, runtime_instance_id_);
}

RecoveryExecutionPlan RecoveryManager::evaluate(
    const contracts::RecoveryRequest& request) const {
  RecoveryExecutionPlan result;
  const auto request_guard = contracts::validate_recovery_request_field_rules(request);
  if (!request_guard.ok) {
    result = make_recovery_rejection_plan(
        make_detail(
            RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED,
            std::string(request_guard.reason)),
        RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED);
  } else if (request.runtime_budget_snapshot.has_value() &&
             snapshot_is_exhausted(*request.runtime_budget_snapshot)) {
    const auto reason = request.runtime_budget_snapshot->overall_reject_reason.value_or(
        std::string("runtime budget exhausted during recovery admission"));
    result = make_degrade_plan(reason);
  } else {
    const auto retry_count =
        request.retry_count.value_or(request.checkpoint->retry_count.value_or(0U));
    {
      const std::lock_guard<std::mutex> lock(recovery_mutex_);
      last_evaluated_retry_count_ = retry_count + 1U;
    }

    switch (*request.reflection_decision->decision_kind) {
      case contracts::ReflectionDecisionKind::AbortSafe:
        result = make_recovery_escalation_plan(
            "reflection requested abort_safe escalation",
            SafeFailureHint{
                .enter_safe_mode = true,
                .enter_degraded_mode = false,
                .reason = "abort_safe should enter a supervised recovery path",
            });
        break;

      case contracts::ReflectionDecisionKind::Replan: {
        if (request.runtime_budget_snapshot.has_value() &&
            !replan_budget_available(*request.runtime_budget_snapshot)) {
          result = make_recovery_rejection_plan(
              make_detail(
                  RuntimeErrorCode::RT_E_304_REPLAN_OVERRUN,
                  "replan budget exhausted"),
              RuntimeErrorCode::RT_E_304_REPLAN_OVERRUN);
          break;
        }

        const auto replan_token = make_replan_token(request);
        result = make_recovery_execution_plan(
            "replan",
            "replan admitted; new retry_idempotency_token=" + replan_token,
            std::nullopt);
        break;
      }

      case contracts::ReflectionDecisionKind::Continue:
      case contracts::ReflectionDecisionKind::RetryStep: {
        const auto& report = *request.idempotency_and_side_effect_report;
        if (!report.replay_safe.value()) {
          result = make_recovery_rejection_plan(
              make_detail(
                  RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED,
                  report.non_replayable_reason.value_or(
                      std::string("replay rejected because side effects are not replay-safe"))),
              RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED);
          break;
        }

        if (!report.idempotency_key.has_value() || report.idempotency_key->empty()) {
          result = make_recovery_rejection_plan(
              make_detail(
                  RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED,
                  "retry_step requires retry_idempotency_token reuse evidence"),
              RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED);
          break;
        }

        const auto resume_decision = checkpoint_manager_.make_resume_plan(*request.checkpoint);
        if (resume_decision.rejected()) {
          result = make_recovery_rejection_plan(
              make_detail(
                  resume_decision.error_code.value_or(RuntimeErrorCode::RT_E_412_RESUME_REJECTED),
                  resume_decision.detail),
              resume_decision.error_code.value_or(RuntimeErrorCode::RT_E_412_RESUME_REJECTED));
          break;
        }

        const auto action = *request.reflection_decision->decision_kind ==
                                    contracts::ReflectionDecisionKind::Continue
                                ? std::string("continue")
                                : std::string("retry_step");
        result = make_recovery_execution_plan(
            action,
            action + " admitted; reuse retry_idempotency_token=" + *report.idempotency_key,
            resume_decision.plan);
        break;
      }

      case contracts::ReflectionDecisionKind::Unspecified:
        result = make_recovery_rejection_plan(
            make_detail(
                RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED,
                "reflection decision must be concrete"),
            RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED);
        break;
    }
  }

  if (result.planned_action.empty()) {
    result = make_recovery_rejection_plan(
        make_detail(
            RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED,
            "recovery decision is not supported"),
        RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED);
  }

  emit_recovery_plan_log(logger_, runtime_instance_id_, request, result);
  return result;
}

contracts::RecoveryOutcome RecoveryManager::execute(
    const RecoveryExecutionPlan& plan) {
  std::optional<std::uint32_t> updated_retry_count;
  {
    const std::lock_guard<std::mutex> lock(recovery_mutex_);
    updated_retry_count = last_evaluated_retry_count_;
  }

  contracts::RecoveryOutcome outcome;
  if (!plan.executable()) {
    if (plan.escalated()) {
      const bool degraded = plan.safe_failure_hint.has_value() &&
                            plan.safe_failure_hint->enter_degraded_mode;
      outcome = contracts::RecoveryOutcome{
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
    } else {
      outcome = contracts::RecoveryOutcome{
          .executed_action = std::string("rejected"),
          .final_runtime_state = std::string("FailedSafe"),
          .updated_retry_count = updated_retry_count,
          .checkpoint_ref = std::nullopt,
          .compensation_result_ref = std::nullopt,
          .rejection_reason = plan.detail,
          .escalation_reason = std::nullopt,
      };
    }

    emit_recovery_outcome_log(logger_, runtime_instance_id_, outcome);
    return outcome;
  }

  const auto final_state = plan.planned_action == "replan"
                               ? std::string("Planning")
                               : plan.resume_plan.has_value()
                                     ? std::string(runtime_state_name(plan.resume_plan->target_state))
                                     : std::string("Reasoning");

  outcome = contracts::RecoveryOutcome{
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
  emit_recovery_outcome_log(logger_, runtime_instance_id_, outcome);
  return outcome;
}

RecoveryApplyResult RecoveryManager::apply(
    const contracts::RecoveryOutcome& outcome) {
  RecoveryApplyResult result;
  const auto guard = contracts::validate_recovery_outcome_field_rules(outcome);
  if (!guard.ok) {
    result = RecoveryApplyResult{
        .applied = false,
        .error_code = RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED,
        .detail = std::string(guard.reason),
    };
    emit_recovery_apply_log(logger_, runtime_instance_id_, outcome, result);
    return result;
  }

  {
    const std::lock_guard<std::mutex> lock(recovery_mutex_);
    last_outcome_ = outcome;
  }

  if (*outcome.executed_action == "rejected") {
    result = RecoveryApplyResult{
        .applied = false,
        .error_code = RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED,
        .detail = outcome.rejection_reason.value_or(
            make_detail(
                RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED,
                "rejected outcomes are not applied")),
    };
    emit_recovery_apply_log(logger_, runtime_instance_id_, outcome, result);
    return result;
  }

  if (*outcome.executed_action == "abort_safe") {
    result = RecoveryApplyResult{
        .applied = true,
        .error_code = RuntimeErrorCode::RT_E_510_SAFE_MODE_ENTERED,
        .detail = make_detail(
            RuntimeErrorCode::RT_E_510_SAFE_MODE_ENTERED,
            "abort_safe outcome applied"),
    };
    emit_recovery_apply_log(logger_, runtime_instance_id_, outcome, result);
    return result;
  }

  if (*outcome.executed_action == "degrade") {
    result = RecoveryApplyResult{
        .applied = true,
        .error_code = RuntimeErrorCode::RT_E_511_DEGRADE_ENTERED,
        .detail = make_detail(
            RuntimeErrorCode::RT_E_511_DEGRADE_ENTERED,
            "degraded recovery outcome applied"),
    };
    emit_recovery_apply_log(logger_, runtime_instance_id_, outcome, result);
    return result;
  }

  result = RecoveryApplyResult{
      .applied = true,
      .error_code = std::nullopt,
      .detail = "recovery outcome applied",
  };
  emit_recovery_apply_log(logger_, runtime_instance_id_, outcome, result);
  return result;
}

const std::optional<contracts::RecoveryOutcome>& RecoveryManager::last_outcome() const {
  return last_outcome_;
}

}  // namespace dasall::runtime