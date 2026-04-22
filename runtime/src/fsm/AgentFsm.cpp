#include "AgentFsm.h"

#include <optional>
#include <string>
#include <utility>

#include "TransitionGuardTable.h"

namespace dasall::runtime {
namespace {

[[nodiscard]] constexpr const char* guard_fact_name(const TransitionGuardFact guard_fact) {
  switch (guard_fact) {
    case TransitionGuardFact::AgentRequestAvailable:
      return "AgentRequestAvailable";
    case TransitionGuardFact::FacadeInitialized:
      return "FacadeInitialized";
    case TransitionGuardFact::RequestValidated:
      return "RequestValidated";
    case TransitionGuardFact::SessionLoaded:
      return "SessionLoaded";
    case TransitionGuardFact::BudgetInitialized:
      return "BudgetInitialized";
    case TransitionGuardFact::ContextAssembled:
      return "ContextAssembled";
    case TransitionGuardFact::ClarificationNeeded:
      return "ClarificationNeeded";
    case TransitionGuardFact::ProfileAllowsClarify:
      return "ProfileAllowsClarify";
    case TransitionGuardFact::HighRiskConfirmationRequired:
      return "HighRiskConfirmationRequired";
    case TransitionGuardFact::UserConfirmationGranted:
      return "UserConfirmationGranted";
    case TransitionGuardFact::ToolCallPlanned:
      return "ToolCallPlanned";
    case TransitionGuardFact::BudgetAllowsToolCall:
      return "BudgetAllowsToolCall";
    case TransitionGuardFact::ToolDispatchSubmitted:
      return "ToolDispatchSubmitted";
    case TransitionGuardFact::ExternalResultAvailable:
      return "ExternalResultAvailable";
    case TransitionGuardFact::ReflectionContinue:
      return "ReflectionContinue";
    case TransitionGuardFact::RecoveryRetryAllowed:
      return "RecoveryRetryAllowed";
    case TransitionGuardFact::RecoveryReplanAllowed:
      return "RecoveryReplanAllowed";
    case TransitionGuardFact::RecoveryAbortSafe:
      return "RecoveryAbortSafe";
    case TransitionGuardFact::RecoveryDegrade:
      return "RecoveryDegrade";
    case TransitionGuardFact::DirectResponseReady:
      return "DirectResponseReady";
    case TransitionGuardFact::ResponseMaterialized:
      return "ResponseMaterialized";
    case TransitionGuardFact::AuditCommitted:
      return "AuditCommitted";
    case TransitionGuardFact::PersistenceConfirmed:
      return "PersistenceConfirmed";
    case TransitionGuardFact::ResultReturned:
      return "ResultReturned";
    case TransitionGuardFact::RecoveryRejected:
      return "RecoveryRejected";
    case TransitionGuardFact::DegradeAllowed:
      return "DegradeAllowed";
    case TransitionGuardFact::SafeModeTriggerSatisfied:
      return "SafeModeTriggerSatisfied";
  }

  return "Unknown";
}

[[nodiscard]] StateTransitionOutcome make_rejected_outcome(
    const StateTransitionRequest& request,
    const RuntimeState current_state,
    const TransitionViolationType violation_type,
    std::string detail,
    const std::optional<TransitionGuardFact> violated_guard = std::nullopt) {
  return StateTransitionOutcome{
      .accepted = false,
      .previous_state = current_state,
      .resolved_state = current_state,
      .transition_reason = request.transition_reason,
      .checkpoint_hint = {},
      .rejection_reason = TransitionRejectionReason{
          .from_state = current_state,
          .requested_to = request.requested_to,
          .violation_type = violation_type,
          .violated_guard = violated_guard,
          .detail = std::move(detail),
      },
  };
}

}  // namespace

AgentFsm::AgentFsm(const RuntimeState initial_state) : current_state_(initial_state) {}

RuntimeState AgentFsm::current_state() const {
  const std::lock_guard<std::mutex> lock(state_mutex_);
  return current_state_;
}

bool AgentFsm::can_enter(const StateTransitionRequest& request) const {
  const std::lock_guard<std::mutex> lock(state_mutex_);
  return evaluate_request_locked(request).accepted;
}

StateTransitionOutcome AgentFsm::transition(const StateTransitionRequest& request) {
  const std::lock_guard<std::mutex> lock(state_mutex_);
  const auto evaluation = evaluate_request_locked(request);
  if (!evaluation.accepted) {
    return make_rejected_outcome(
        request,
        current_state_,
        evaluation.violation_type,
        evaluation.detail,
        evaluation.violated_guard);
  }

  current_state_ = request.requested_to;
  return make_transition_outcome(request, current_state_, *evaluation.checkpoint_hint);
}

bool AgentFsm::is_terminal(const RuntimeState state) const {
  return state == RuntimeState::SafeMode;
}

AgentFsm::EvaluationResult AgentFsm::evaluate_request_locked(
    const StateTransitionRequest& request) const {
  if (request.from_state != current_state_) {
    return EvaluationResult{
        .accepted = false,
        .checkpoint_hint = std::nullopt,
        .violation_type = TransitionViolationType::SourceStateMismatch,
        .violated_guard = std::nullopt,
        .detail = std::string("request.from_state=") +
                  runtime_state_name(request.from_state) + " does not match current_state=" +
                  runtime_state_name(current_state_),
    };
  }

  if (is_terminal(current_state_) && request.requested_to != current_state_) {
    return EvaluationResult{
        .accepted = false,
        .checkpoint_hint = std::nullopt,
        .violation_type = TransitionViolationType::TerminalStateExit,
        .violated_guard = std::nullopt,
        .detail = std::string("cannot leave terminal state ") +
                  runtime_state_name(current_state_),
    };
  }

  if (!TransitionGuardTable::is_legal(request.from_state, request.requested_to)) {
    return EvaluationResult{
        .accepted = false,
        .checkpoint_hint = std::nullopt,
        .violation_type = TransitionViolationType::IllegalTransition,
        .violated_guard = std::nullopt,
        .detail = std::string("transition ") + runtime_state_name(request.from_state) +
                  " -> " + runtime_state_name(request.requested_to) +
                  " is not legal in section 6.7.4",
    };
  }

  const auto guard_rule = TransitionGuardTable::get_guard(request.from_state, request.requested_to);
  if (!guard_rule.has_value()) {
    return EvaluationResult{
        .accepted = false,
        .checkpoint_hint = std::nullopt,
        .violation_type = TransitionViolationType::IllegalTransition,
        .violated_guard = std::nullopt,
        .detail = std::string("transition rule lookup failed for legal edge ") +
                  runtime_state_name(request.from_state) + " -> " +
                  runtime_state_name(request.requested_to),
    };
  }

  const auto violated_guard = guard_rule->first_unsatisfied_guard(request);
  if (violated_guard.has_value()) {
    return EvaluationResult{
        .accepted = false,
        .checkpoint_hint = std::nullopt,
        .violation_type = TransitionViolationType::MissingGuardFact,
        .violated_guard = violated_guard,
        .detail = std::string("missing required guard fact ") +
                  guard_fact_name(*violated_guard) + " for transition " +
                  runtime_state_name(request.from_state) + " -> " +
                  runtime_state_name(request.requested_to),
    };
  }

  const auto checkpoint_hint =
      TransitionGuardTable::get_checkpoint_strategy(request.from_state, request.requested_to);
  if (!checkpoint_hint.has_value()) {
    return EvaluationResult{
        .accepted = false,
        .checkpoint_hint = std::nullopt,
        .violation_type = TransitionViolationType::IllegalTransition,
        .violated_guard = std::nullopt,
        .detail = std::string("checkpoint strategy lookup failed for legal edge ") +
                  runtime_state_name(request.from_state) + " -> " +
                  runtime_state_name(request.requested_to),
    };
  }

  return EvaluationResult{
      .accepted = true,
      .checkpoint_hint = checkpoint_hint,
      .violation_type = TransitionViolationType::IllegalTransition,
      .violated_guard = std::nullopt,
      .detail = {},
  };
}

}  // namespace dasall::runtime