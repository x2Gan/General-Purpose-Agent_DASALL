#pragma once

#include <unordered_map>
#include <mutex>
#include <optional>
#include <string>

#include "checkpoint/ICheckpointManager.h"
#include "session/SessionTypes.h"

namespace dasall::runtime {

class CheckpointManager final : public ICheckpointManager {
 public:
  CheckpointManager() = default;

  [[nodiscard]] CheckpointBuildResult build_checkpoint(
      const CheckpointBuildRequest& request) const override;
  [[nodiscard]] CheckpointPersistResult save(const contracts::Checkpoint& checkpoint) override;
  [[nodiscard]] CheckpointPersistResult save(
      const contracts::Checkpoint& checkpoint,
      const std::optional<contracts::BudgetSnapshot>& runtime_budget_snapshot);
  [[nodiscard]] CheckpointLoadResult load(const std::string& checkpoint_ref) const override;
  [[nodiscard]] CheckpointConsistencyReport validate(
      const contracts::Checkpoint& checkpoint) const override;
  [[nodiscard]] ResumePlanDecision make_resume_plan(
      const contracts::Checkpoint& checkpoint) const override;
  [[nodiscard]] ResumePlanDecision make_resume_plan(
      const contracts::Checkpoint& checkpoint,
      const ResumeSeed& resume_seed) const;

  void seed_for_test(
      const contracts::Checkpoint& checkpoint,
      std::optional<contracts::BudgetSnapshot> runtime_budget_snapshot = std::nullopt);

 private:
  struct StoredCheckpointRecord {
    contracts::Checkpoint checkpoint;
    std::optional<contracts::BudgetSnapshot> runtime_budget_snapshot;
  };

  mutable std::mutex ckpt_mutex_;
    mutable std::unordered_map<std::string, std::optional<contracts::BudgetSnapshot>>
            pending_runtime_budget_snapshots_;
  std::unordered_map<std::string, StoredCheckpointRecord> stored_checkpoints_;
};

}  // namespace dasall::runtime