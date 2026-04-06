#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "metrics/IMetricsProvider.h"
#include "metrics/MetricsErrors.h"
#include "metrics/MetricsSnapshots.h"

namespace dasall::infra::metrics {

struct MetricsRecoveryEvent {
  std::string action;
  std::string stage;
  std::string reason;
  std::optional<MetricsErrorCode> error_code;
  MetricsModuleSnapshot module_snapshot{};
  std::uint64_t consecutive_failure_total = 0;
  std::uint64_t degrade_enter_total = 0;
  std::uint64_t recovery_success_total = 0;

  [[nodiscard]] bool is_valid() const {
    return !action.empty() && !stage.empty() && !reason.empty() &&
           module_snapshot.is_valid();
  }
};

class IMetricsRecoveryLogHook {
 public:
  virtual ~IMetricsRecoveryLogHook() = default;

  virtual MetricsOperationStatus write_recovery_event(
      const MetricsRecoveryEvent& event) = 0;
};

class MetricsRecovery {
 public:
  explicit MetricsRecovery(
      std::uint32_t consecutive_failure_threshold = 2U,
      std::shared_ptr<IMetricsRecoveryLogHook> log_hook = {});

  MetricsOperationStatus observe_export_result(
      const MetricsOperationStatus& export_status,
      const MetricsModuleSnapshot& exporter_snapshot);
  MetricsOperationStatus enter_degraded(MetricsErrorCode error_code,
                                        std::string reason,
                                        MetricsModuleSnapshot snapshot);
  MetricsOperationStatus recover_to_healthy(std::string reason,
                                            MetricsModuleSnapshot snapshot);
  MetricsOperationStatus emit_recovery_event(std::string stage);

  [[nodiscard]] bool is_degraded() const;
  [[nodiscard]] std::uint64_t consecutive_failure_total() const;
  [[nodiscard]] std::uint64_t degrade_enter_total() const;
  [[nodiscard]] std::uint64_t recovery_success_total() const;
  [[nodiscard]] const std::optional<MetricsRecoveryEvent>& last_event() const;
  [[nodiscard]] const MetricsModuleSnapshot& module_snapshot() const;
  [[nodiscard]] const std::string& last_failure_reason() const;
  [[nodiscard]] std::optional<MetricsErrorCode> last_error_code() const;

 private:
  [[nodiscard]] MetricsOperationStatus invalid_request(std::string message,
                                                       std::string stage) const;
  [[nodiscard]] static std::optional<MetricsErrorCode> infer_error_code(
      const MetricsOperationStatus& export_status);
  void prepare_event(std::string action,
                     std::string reason,
                     std::optional<MetricsErrorCode> error_code,
                     const MetricsModuleSnapshot& snapshot);

  std::uint32_t consecutive_failure_threshold_ = 2U;
  std::shared_ptr<IMetricsRecoveryLogHook> log_hook_;
  bool degraded_ = false;
  std::uint64_t consecutive_failure_total_ = 0;
  std::uint64_t degrade_enter_total_ = 0;
  std::uint64_t recovery_success_total_ = 0;
  MetricsModuleSnapshot module_snapshot_{};
  std::string last_failure_reason_;
  std::optional<MetricsErrorCode> last_error_code_;
  std::optional<MetricsRecoveryEvent> last_event_;
};

}  // namespace dasall::infra::metrics