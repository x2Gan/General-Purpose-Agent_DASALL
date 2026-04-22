#include "SafeModeController.h"

#include <string_view>

namespace dasall::runtime {
namespace {

[[nodiscard]] bool fallback_step_enabled(
    const profiles::DegradePolicy& degrade_policy,
    const std::string_view step) {
  if (step == "allow_model_failover") {
    return degrade_policy.allow_model_failover;
  }

  if (step == "allow_budget_degrade") {
    return degrade_policy.allow_budget_degrade;
  }

  if (step == "allow_tool_skip") {
    return true;
  }

  if (step == "abort_safe") {
    return true;
  }

  return false;
}

[[nodiscard]] std::optional<RuntimeState> runtime_state_for_mode(const SafeModeState mode) {
  switch (mode) {
    case SafeModeState::FailedSafe:
      return RuntimeState::FailedSafe;
    case SafeModeState::Degraded:
      return RuntimeState::Degraded;
    case SafeModeState::SafeMode:
      return RuntimeState::SafeMode;
    case SafeModeState::Normal:
      return std::nullopt;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<RuntimeErrorCode> error_code_for_mode(const SafeModeState mode) {
  switch (mode) {
    case SafeModeState::FailedSafe:
      return RuntimeErrorCode::RT_E_501_RECOVERY_ESCALATED;
    case SafeModeState::Degraded:
      return RuntimeErrorCode::RT_E_511_DEGRADE_ENTERED;
    case SafeModeState::SafeMode:
      return RuntimeErrorCode::RT_E_510_SAFE_MODE_ENTERED;
    case SafeModeState::Normal:
      return std::nullopt;
  }

  return std::nullopt;
}

}  // namespace

SafeModeController::SafeModeController(
    std::shared_ptr<const profiles::RuntimePolicySnapshot> policy_snapshot)
    : policy_snapshot_(std::move(policy_snapshot)) {}

std::optional<std::string> SafeModeController::select_fallback(
    const SafeModeTriggerKind trigger_kind) const {
  if (policy_snapshot_ == nullptr) {
    return std::nullopt;
  }

  const auto& degrade_policy = policy_snapshot_->degrade_policy();
  if (trigger_kind == SafeModeTriggerKind::BudgetExhausted) {
    if (degrade_policy.allow_budget_degrade) {
      return std::string("allow_budget_degrade");
    }

    for (const auto& step : degrade_policy.fallback_chain) {
      if (step == "abort_safe") {
        return step;
      }
    }

    return std::nullopt;
  }

  if (trigger_kind == SafeModeTriggerKind::DependencyUnavailable &&
      degrade_policy.allow_model_failover) {
    for (const auto& step : degrade_policy.fallback_chain) {
      if (!step.empty() && step != "abort_safe") {
        return step;
      }
    }
  }

  for (const auto& step : degrade_policy.fallback_chain) {
    if (!fallback_step_enabled(degrade_policy, step)) {
      continue;
    }

    if (trigger_kind == SafeModeTriggerKind::DependencyUnavailable) {
      return step;
    }
  }

  return std::nullopt;
}

SafeModeDecision SafeModeController::no_transition(
    const std::string& detail,
    const SafeModeState state) const {
  return SafeModeDecision{
      .transition_required = false,
      .previous_mode = state,
      .target_mode = state,
      .action = SafeModeAction::None,
      .target_runtime_state = runtime_state_for_mode(state),
      .error_code = error_code_for_mode(state),
      .selected_fallback = std::nullopt,
      .detail = detail,
  };
}

SafeModeDecision SafeModeController::transition_locked(
    const SafeModeState target_mode,
    const SafeModeAction action,
    std::optional<std::string> selected_fallback,
    std::string detail) {
  const SafeModeState previous_mode = current_mode_;
  current_mode_ = target_mode;
  if (detail.empty()) {
    detail = std::string("safe mode transition -> ") + safe_mode_state_name(target_mode);
  }

  return SafeModeDecision{
      .transition_required = previous_mode != target_mode,
      .previous_mode = previous_mode,
      .target_mode = target_mode,
      .action = action,
      .target_runtime_state = runtime_state_for_mode(target_mode),
      .error_code = error_code_for_mode(target_mode),
      .selected_fallback = std::move(selected_fallback),
      .detail = std::move(detail),
  };
}

SafeModeDecision SafeModeController::evaluate_entry(const SafeModeTrigger& trigger) {
  const std::lock_guard<std::mutex> lock(mode_mutex_);

  switch (trigger.trigger_kind) {
    case SafeModeTriggerKind::PolicyForbidden:
      return transition_locked(
          SafeModeState::SafeMode,
          SafeModeAction::EnterSafeMode,
          std::nullopt,
          trigger.detail.empty() ? "policy forbids continuing execution" : trigger.detail);

    case SafeModeTriggerKind::WatchdogTimeout:
      return transition_locked(
          SafeModeState::SafeMode,
          SafeModeAction::EnterSafeMode,
          std::nullopt,
          trigger.detail.empty() ? "watchdog timeout forces safe mode" : trigger.detail);

    case SafeModeTriggerKind::BudgetExhausted: {
      const auto selected_fallback = select_fallback(trigger.trigger_kind);
      if (selected_fallback.has_value() && *selected_fallback == "allow_budget_degrade") {
        return transition_locked(
            SafeModeState::Degraded,
            SafeModeAction::EnterDegraded,
            selected_fallback,
            trigger.detail.empty() && trigger.budget_decision.has_value()
                ? trigger.budget_decision->detail
                : trigger.detail);
      }

      return transition_locked(
          SafeModeState::FailedSafe,
          SafeModeAction::EnterFailedSafe,
          selected_fallback,
          trigger.detail.empty() && trigger.budget_decision.has_value()
              ? trigger.budget_decision->detail
              : trigger.detail);
    }

    case SafeModeTriggerKind::DependencyUnavailable: {
      const auto selected_fallback = select_fallback(trigger.trigger_kind);
      if (selected_fallback.has_value() && *selected_fallback != "abort_safe") {
        return transition_locked(
            SafeModeState::Degraded,
            SafeModeAction::EnterDegraded,
            selected_fallback,
            trigger.detail.empty() && trigger.health_signal.has_value()
                ? trigger.health_signal->detail
                : trigger.detail);
      }

      return transition_locked(
          SafeModeState::FailedSafe,
          SafeModeAction::EnterFailedSafe,
          selected_fallback,
          trigger.detail.empty() && trigger.health_signal.has_value()
              ? trigger.health_signal->detail
              : trigger.detail);
    }

    case SafeModeTriggerKind::RecoveryExhausted: {
      if (trigger.recovery_outcome.has_value()) {
        const auto& recovery_outcome = *trigger.recovery_outcome;
        if (recovery_outcome.executed_action == std::optional<std::string>("degrade") ||
            recovery_outcome.final_runtime_state == std::optional<std::string>("Degraded")) {
          return transition_locked(
              SafeModeState::Degraded,
              SafeModeAction::EnterDegraded,
              recovery_outcome.executed_action,
              trigger.detail.empty()
                  ? recovery_outcome.escalation_reason.value_or(
                        std::string("recovery exhausted into degraded mode"))
                  : trigger.detail);
        }

        if (recovery_outcome.final_runtime_state == std::optional<std::string>("SafeMode")) {
          return transition_locked(
              SafeModeState::SafeMode,
              SafeModeAction::EnterSafeMode,
              recovery_outcome.executed_action,
              trigger.detail.empty()
                  ? recovery_outcome.escalation_reason.value_or(
                        std::string("recovery exhausted into safe mode"))
                  : trigger.detail);
        }
      }

      return transition_locked(
          SafeModeState::FailedSafe,
          SafeModeAction::EnterFailedSafe,
          trigger.recovery_outcome.has_value() ? trigger.recovery_outcome->executed_action
                                               : std::nullopt,
          trigger.detail.empty() ? "recovery exhausted without a viable continuation path"
                                 : trigger.detail);
    }
  }

  return no_transition("unknown safe mode trigger", current_mode_);
}

SafeModeDecision SafeModeController::evaluate_exit(const SafeModeExitRequest& request) {
  const std::lock_guard<std::mutex> lock(mode_mutex_);
  if (current_mode_ == SafeModeState::Normal) {
    return no_transition("runtime already in normal mode", current_mode_);
  }

  if (!request.dependencies_healthy || !request.watchdog_healthy || !request.operator_cleared ||
      !request.budget_restored) {
    return no_transition(
        request.detail.empty() ? "exit rejected because recovery conditions are incomplete"
                               : request.detail,
        current_mode_);
  }

  return transition_locked(
      SafeModeState::Normal,
      SafeModeAction::ExitToNormal,
      std::nullopt,
      request.detail.empty() ? "safe mode exit accepted" : request.detail);
}

SafeModeState SafeModeController::current_mode() const {
  const std::lock_guard<std::mutex> lock(mode_mutex_);
  return current_mode_;
}

}  // namespace dasall::runtime