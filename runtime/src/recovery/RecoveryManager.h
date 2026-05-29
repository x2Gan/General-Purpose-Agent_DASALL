#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "../checkpoint/CheckpointManager.h"
#include "recovery/IRecoveryManager.h"

namespace dasall::infra::logging {
class ILogger;
}

namespace dasall::runtime {

class RecoveryManager final : public IRecoveryManager {
 public:
  RecoveryManager() = default;

    void set_logger(
            std::shared_ptr<infra::logging::ILogger> logger,
            std::optional<std::string> runtime_instance_id = std::nullopt);

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
    std::shared_ptr<infra::logging::ILogger> logger_;
    std::optional<std::string> runtime_instance_id_;
};

}  // namespace dasall::runtime