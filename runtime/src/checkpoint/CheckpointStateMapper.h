#pragma once

#include <optional>

#include "checkpoint/Checkpoint.h"
#include "fsm/StateTransitionTypes.h"

namespace dasall::runtime::CheckpointStateMapper {

[[nodiscard]] std::optional<contracts::CheckpointState> to_checkpoint_state(
    RuntimeState runtime_state);

[[nodiscard]] bool can_resume_from(contracts::CheckpointState checkpoint_state);

}  // namespace dasall::runtime::CheckpointStateMapper