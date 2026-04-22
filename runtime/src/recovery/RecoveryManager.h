#pragma once

#include <mutex>
#include <optional>
#include <string>

#include "../checkpoint/CheckpointManager.h"
#include "recovery/IRecoveryManager.h"

namespace dasall::runtime {

class RecoveryManager final : public IRecoveryManager {
 public:
  RecoveryManager() = default;

  [[nodiscard]] RecoveryExecutionPlan evaluate(
      const contracts::RecoveryRequest& request) const override;
  [[nodiscard]] contracts::RecoveryOutcome execute(
      const RecoveryExecutionPlan& plan) override;
  [[nodiscard]] RecoveryApplyResult apply(
      const contracts::RecoveryOutcome& outcome) override;

  [[nodiscard]] const std::optional<contracts::RecoveryOutcome>& last_outcome() const;

 private:
  mutable std::mutex recovery_mutex_;
  mutable std::optional<std::uint32_t> last_evaluated_retry_count_;
  CheckpointManager checkpoint_manager_;
  std::optional<contracts::RecoveryOutcome> last_outcome_;
};

}  // namespace dasall::runtime