#pragma once

#include <string>

#include "checkpoint/CheckpointBuildTypes.h"
#include "recovery/ResumePlan.h"

namespace dasall::runtime {

class ICheckpointManager {
 public:
  virtual ~ICheckpointManager() = default;

  [[nodiscard]] virtual CheckpointBuildResult build_checkpoint(
      const CheckpointBuildRequest& request) const = 0;

  [[nodiscard]] virtual CheckpointPersistResult save(
      const contracts::Checkpoint& checkpoint) = 0;

  [[nodiscard]] virtual CheckpointLoadResult load(
      const std::string& checkpoint_ref) const = 0;

  [[nodiscard]] virtual CheckpointConsistencyReport validate(
      const contracts::Checkpoint& checkpoint) const = 0;

  [[nodiscard]] virtual ResumePlanDecision make_resume_plan(
      const contracts::Checkpoint& checkpoint) const = 0;
};

}  // namespace dasall::runtime