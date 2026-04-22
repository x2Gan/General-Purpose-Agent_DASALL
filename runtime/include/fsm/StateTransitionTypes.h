#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "checkpoint/Checkpoint.h"

namespace dasall::runtime {

enum class RuntimeState : std::uint8_t {
  Idle = 0,
  Receiving,
  Planning,
  Reasoning,
  WaitingClarify,
  WaitingConfirm,
  ToolCalling,
  WaitingExternal,
  Reflecting,
  FailedSafe,
  Responding,
  Auditing,
  Persisting,
  Completed,
  Failed,
  Degraded,
  SafeMode,
};

[[nodiscard]] constexpr std::array<RuntimeState, 17> runtime_state_catalog() {
  return {RuntimeState::Idle,
          RuntimeState::Receiving,
          RuntimeState::Planning,
          RuntimeState::Reasoning,
          RuntimeState::WaitingClarify,
          RuntimeState::WaitingConfirm,
          RuntimeState::ToolCalling,
          RuntimeState::WaitingExternal,
          RuntimeState::Reflecting,
          RuntimeState::FailedSafe,
          RuntimeState::Responding,
          RuntimeState::Auditing,
          RuntimeState::Persisting,
          RuntimeState::Completed,
          RuntimeState::Failed,
          RuntimeState::Degraded,
          RuntimeState::SafeMode};
}

[[nodiscard]] constexpr const char* runtime_state_name(const RuntimeState state) {
  switch (state) {
    case RuntimeState::Idle:
      return "Idle";
    case RuntimeState::Receiving:
      return "Receiving";
    case RuntimeState::Planning:
      return "Planning";
    case RuntimeState::Reasoning:
      return "Reasoning";
    case RuntimeState::WaitingClarify:
      return "WaitingClarify";
    case RuntimeState::WaitingConfirm:
      return "WaitingConfirm";
    case RuntimeState::ToolCalling:
      return "ToolCalling";
    case RuntimeState::WaitingExternal:
      return "WaitingExternal";
    case RuntimeState::Reflecting:
      return "Reflecting";
    case RuntimeState::FailedSafe:
      return "FailedSafe";
    case RuntimeState::Responding:
      return "Responding";
    case RuntimeState::Auditing:
      return "Auditing";
    case RuntimeState::Persisting:
      return "Persisting";
    case RuntimeState::Completed:
      return "Completed";
    case RuntimeState::Failed:
      return "Failed";
    case RuntimeState::Degraded:
      return "Degraded";
    case RuntimeState::SafeMode:
      return "SafeMode";
  }

  return "Unknown";
}

enum class TransitionGuardFact : std::uint8_t {
  AgentRequestAvailable = 0,
  FacadeInitialized,
  RequestValidated,
  SessionLoaded,
  BudgetInitialized,
  ContextAssembled,
  ClarificationNeeded,
  ProfileAllowsClarify,
  HighRiskConfirmationRequired,
  UserConfirmationGranted,
  ToolCallPlanned,
  BudgetAllowsToolCall,
  ToolDispatchSubmitted,
  ExternalResultAvailable,
  ReflectionContinue,
  RecoveryRetryAllowed,
  RecoveryReplanAllowed,
  RecoveryAbortSafe,
  RecoveryDegrade,
  DirectResponseReady,
  ResponseMaterialized,
  AuditCommitted,
  PersistenceConfirmed,
  ResultReturned,
  RecoveryRejected,
  DegradeAllowed,
  SafeModeTriggerSatisfied,
};

enum class TransitionViolationType : std::uint8_t {
  IllegalTransition = 0,
  SourceStateMismatch,
  MissingGuardFact,
  GuardRejected,
  TerminalStateExit,
};

enum class CheckpointMutationKind : std::uint8_t {
  None = 0,
  Write,
  Update,
  Retain,
  ClearActiveReference,
};

struct StateTransitionCheckpointHint {
  CheckpointMutationKind mutation = CheckpointMutationKind::None;
  std::optional<contracts::CheckpointState> checkpoint_state;
  bool pending_action_required = false;

  [[nodiscard]] bool writes_checkpoint() const {
    return mutation == CheckpointMutationKind::Write ||
           mutation == CheckpointMutationKind::Update;
  }
};

struct StateTransitionRequest {
  RuntimeState from_state = RuntimeState::Idle;
  RuntimeState requested_to = RuntimeState::Idle;
  std::string transition_reason;
  std::vector<TransitionGuardFact> guard_facts;

  [[nodiscard]] bool has_guard(const TransitionGuardFact fact) const {
    for (const auto candidate : guard_facts) {
      if (candidate == fact) {
        return true;
      }
    }
    return false;
  }
};

struct TransitionRejectionReason {
  RuntimeState from_state = RuntimeState::Idle;
  RuntimeState requested_to = RuntimeState::Idle;
  TransitionViolationType violation_type = TransitionViolationType::IllegalTransition;
  std::optional<TransitionGuardFact> violated_guard;
  std::string detail;
};

struct StateTransitionOutcome {
  bool accepted = false;
  RuntimeState previous_state = RuntimeState::Idle;
  RuntimeState resolved_state = RuntimeState::Idle;
  std::string transition_reason;
  StateTransitionCheckpointHint checkpoint_hint;
  std::optional<TransitionRejectionReason> rejection_reason;

  [[nodiscard]] bool has_rejection() const {
    return rejection_reason.has_value();
  }
};

[[nodiscard]] inline StateTransitionOutcome make_transition_outcome(
    const StateTransitionRequest& request,
    const RuntimeState resolved_state,
    const StateTransitionCheckpointHint checkpoint_hint) {
  return StateTransitionOutcome{
      .accepted = true,
      .previous_state = request.from_state,
      .resolved_state = resolved_state,
      .transition_reason = request.transition_reason,
      .checkpoint_hint = checkpoint_hint,
      .rejection_reason = std::nullopt,
  };
}

[[nodiscard]] inline StateTransitionOutcome make_rejected_transition_outcome(
    const StateTransitionRequest& request,
    const TransitionViolationType violation_type,
    const std::string& detail,
    const std::optional<TransitionGuardFact> violated_guard = std::nullopt) {
  return StateTransitionOutcome{
      .accepted = false,
      .previous_state = request.from_state,
      .resolved_state = request.from_state,
      .transition_reason = request.transition_reason,
      .checkpoint_hint = {},
      .rejection_reason = TransitionRejectionReason{
          .from_state = request.from_state,
          .requested_to = request.requested_to,
          .violation_type = violation_type,
          .violated_guard = violated_guard,
          .detail = detail,
      },
  };
}

}  // namespace dasall::runtime