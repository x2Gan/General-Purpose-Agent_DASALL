#pragma once

#include <mutex>
#include <optional>
#include <string>

#include "checkpoint/ICheckpointManager.h"

namespace dasall::runtime {

class CheckpointManager final : public ICheckpointManager {
 public:
  CheckpointManager() = default;

  [[nodiscard]] CheckpointBuildResult build_checkpoint(
      const CheckpointBuildRequest& request) const override;
  [[nodiscard]] CheckpointPersistResult save(const contracts::Checkpoint& checkpoint) override;
  [[nodiscard]] CheckpointLoadResult load(const std::string& checkpoint_ref) const override;
  [[nodiscard]] CheckpointConsistencyReport validate(
      const contracts::Checkpoint& checkpoint) const override;
  [[nodiscard]] ResumePlanDecision make_resume_plan(
      const contracts::Checkpoint& checkpoint) const override;

  void seed_for_test(const contracts::Checkpoint& checkpoint);

 private:
  mutable std::mutex ckpt_mutex_;
  std::optional<contracts::Checkpoint> stored_checkpoint_;
};

}  // namespace dasall::runtime