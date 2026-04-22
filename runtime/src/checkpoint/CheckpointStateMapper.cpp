#include "CheckpointStateMapper.h"

namespace dasall::runtime::CheckpointStateMapper {

std::optional<contracts::CheckpointState> to_checkpoint_state(
    const RuntimeState runtime_state) {
  using contracts::CheckpointState;

  switch (runtime_state) {
    case RuntimeState::Idle:
      return CheckpointState::Unspecified;
    case RuntimeState::Receiving:
    case RuntimeState::Planning:
    case RuntimeState::Reasoning:
    case RuntimeState::ToolCalling:
    case RuntimeState::Reflecting:
    case RuntimeState::Responding:
    case RuntimeState::Auditing:
    case RuntimeState::Persisting:
      return CheckpointState::Running;
    case RuntimeState::WaitingClarify:
      return CheckpointState::Paused;
    case RuntimeState::WaitingConfirm:
      return CheckpointState::WaitingConfirm;
    case RuntimeState::WaitingExternal:
      return CheckpointState::WaitingTool;
    case RuntimeState::Completed:
      return CheckpointState::Succeeded;
    case RuntimeState::Failed:
    case RuntimeState::FailedSafe:
    case RuntimeState::Degraded:
    case RuntimeState::SafeMode:
      return CheckpointState::Failed;
  }

  return std::nullopt;
}

bool can_resume_from(const contracts::CheckpointState checkpoint_state) {
  switch (checkpoint_state) {
    case contracts::CheckpointState::Running:
    case contracts::CheckpointState::Paused:
    case contracts::CheckpointState::WaitingConfirm:
    case contracts::CheckpointState::WaitingTool:
      return true;
    case contracts::CheckpointState::Failed:
    case contracts::CheckpointState::Succeeded:
    case contracts::CheckpointState::Unspecified:
      return false;
  }

  return false;
}

}  // namespace dasall::runtime::CheckpointStateMapper