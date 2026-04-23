#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "RuntimeErrorCode.h"
#include "checkpoint/Checkpoint.h"
#include "fsm/StateTransitionTypes.h"

namespace dasall::runtime {

enum class ResumePlanViolation : std::uint8_t {
  None = 0,
  CheckpointInvalid,
  MissingCheckpointReference,
  UnsupportedCheckpointState,
  MissingPendingAction,
};

[[nodiscard]] constexpr const char* resume_plan_violation_name(
    const ResumePlanViolation violation) {
  switch (violation) {
    case ResumePlanViolation::None:
      return "None";
    case ResumePlanViolation::CheckpointInvalid:
      return "CheckpointInvalid";
    case ResumePlanViolation::MissingCheckpointReference:
      return "MissingCheckpointReference";
    case ResumePlanViolation::UnsupportedCheckpointState:
      return "UnsupportedCheckpointState";
    case ResumePlanViolation::MissingPendingAction:
      return "MissingPendingAction";
  }

  return "Unknown";
}

[[nodiscard]] constexpr std::optional<RuntimeErrorCode> resume_plan_violation_error_code(
    const ResumePlanViolation violation) {
  switch (violation) {
    case ResumePlanViolation::None:
      return std::nullopt;
    case ResumePlanViolation::CheckpointInvalid:
    case ResumePlanViolation::MissingCheckpointReference:
    case ResumePlanViolation::MissingPendingAction:
      return RuntimeErrorCode::RT_E_202_STATE_INCONSISTENT;
    case ResumePlanViolation::UnsupportedCheckpointState:
      return RuntimeErrorCode::RT_E_412_RESUME_REJECTED;
  }

  return std::nullopt;
}

[[nodiscard]] constexpr std::optional<RuntimeState> resume_target_state(
    const contracts::CheckpointState checkpoint_state) {
  switch (checkpoint_state) {
    case contracts::CheckpointState::Running:
      return RuntimeState::Reasoning;
    case contracts::CheckpointState::Paused:
      return RuntimeState::WaitingClarify;
    case contracts::CheckpointState::WaitingConfirm:
      return RuntimeState::WaitingConfirm;
    case contracts::CheckpointState::WaitingTool:
      return RuntimeState::WaitingExternal;
    case contracts::CheckpointState::Failed:
    case contracts::CheckpointState::Succeeded:
    case contracts::CheckpointState::Unspecified:
      return std::nullopt;
  }

  return std::nullopt;
}

struct ResumePlan {
  std::string checkpoint_ref;
  RuntimeState target_state = RuntimeState::Idle;
  contracts::CheckpointState checkpoint_state = contracts::CheckpointState::Unspecified;
  std::string resume_token;
  std::string resume_reason;
  std::optional<std::string> pending_action;
  std::optional<std::string> policy_snapshot_ref;
  bool requires_operator_intervention = false;

  [[nodiscard]] bool has_pending_action() const {
    return pending_action.has_value() && !pending_action->empty();
  }
};

struct ResumePlanDecision {
  bool resumable = false;
  std::optional<ResumePlan> plan;
  ResumePlanViolation violation = ResumePlanViolation::None;
  std::optional<RuntimeErrorCode> error_code;
  std::string detail;

  [[nodiscard]] bool rejected() const {
    return !resumable;
  }
};

[[nodiscard]] inline ResumePlanDecision make_resume_plan_decision(
    const ResumePlan& plan,
    const std::string& detail = std::string()) {
  return ResumePlanDecision{
      .resumable = true,
      .plan = plan,
      .violation = ResumePlanViolation::None,
      .error_code = std::nullopt,
      .detail = detail,
  };
}

[[nodiscard]] inline ResumePlanDecision make_rejected_resume_plan(
    const ResumePlanViolation violation,
    const std::string& detail,
    const std::optional<RuntimeErrorCode> error_code = std::nullopt) {
  return ResumePlanDecision{
      .resumable = false,
      .plan = std::nullopt,
      .violation = violation,
      .error_code = error_code.has_value() ? error_code
                                           : resume_plan_violation_error_code(violation),
      .detail = detail,
  };
}

}  // namespace dasall::runtime