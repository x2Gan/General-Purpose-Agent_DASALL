#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ota/IBootControlAdapter.h"
#include "ota/IInstallExecutor.h"

namespace dasall::infra::ota {

struct SlotInventory {
  std::string slot_group;
  std::string active_slot;
  std::vector<std::string> candidate_slots;

  [[nodiscard]] bool is_valid() const {
    return !slot_group.empty() && !active_slot.empty() &&
           !candidate_slots.empty() &&
           has_unique_non_empty_values(candidate_slots) &&
           std::find(candidate_slots.begin(), candidate_slots.end(), active_slot) !=
               candidate_slots.end();
  }
};

class ISlotInventoryProvider {
 public:
  virtual ~ISlotInventoryProvider() = default;

  [[nodiscard]] virtual SlotInventory describe_slot_group(
      std::string_view slot_group) const = 0;
};

class IRollbackTokenFactory {
 public:
  virtual ~IRollbackTokenFactory() = default;

  [[nodiscard]] virtual RollbackToken make_token(
      const SlotPlan& slot_plan,
      const std::vector<InstallEvidence>& staged_evidence) const = 0;
};

struct SwitchPolicySnapshot {
  std::string switch_policy = "confirm_after_boot";
  std::string confirm_deadline;

  [[nodiscard]] bool is_valid() const {
    return !switch_policy.empty() && !confirm_deadline.empty();
  }
};

struct SlotSelectionResult {
  bool selected = false;
  std::string active_slot;
  std::string target_slot;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static SlotSelectionResult success(std::string active_slot,
                                                   std::string target_slot) {
    return SlotSelectionResult{
        .selected = true,
        .active_slot = std::move(active_slot),
        .target_slot = std::move(target_slot),
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static SlotSelectionResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return SlotSelectionResult{
        .selected = false,
        .active_slot = {},
        .target_slot = {},
        .result_code = result_code,
        .error = contracts::ErrorInfo{
            .failure_type = contracts::classify_result_code(result_code),
            .retryable = false,
            .safe_to_replan = false,
            .details = contracts::ErrorDetails{
                .code = static_cast<int>(result_code),
                .message = std::move(message),
                .stage = std::move(stage),
            },
            .source_ref = contracts::ErrorSourceRefMinimal{
                .ref_type = "infra.ota",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error.has_value()) {
      return selected && !active_slot.empty() && !target_slot.empty() &&
             active_slot != target_slot;
    }

    return error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(result_code);
  }
};

struct SlotSwitchPreparationResult {
  bool prepared = false;
  SlotPlan slot_plan;
  RollbackToken rollback_token;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static SlotSwitchPreparationResult success(
      SlotPlan slot_plan,
      RollbackToken rollback_token) {
    return SlotSwitchPreparationResult{
        .prepared = true,
        .slot_plan = std::move(slot_plan),
        .rollback_token = std::move(rollback_token),
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static SlotSwitchPreparationResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return SlotSwitchPreparationResult{
        .prepared = false,
        .slot_plan = {},
        .rollback_token = {},
        .result_code = result_code,
        .error = contracts::ErrorInfo{
            .failure_type = contracts::classify_result_code(result_code),
            .retryable = false,
            .safe_to_replan = false,
            .details = contracts::ErrorDetails{
                .code = static_cast<int>(result_code),
                .message = std::move(message),
                .stage = std::move(stage),
            },
            .source_ref = contracts::ErrorSourceRefMinimal{
                .ref_type = "infra.ota",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error.has_value()) {
      return prepared && slot_plan.is_valid() && rollback_token.is_valid();
    }

    return error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(result_code);
  }
};

class SlotSwitchCoordinator {
 public:
  struct Dependencies {
    const ISlotInventoryProvider* slot_inventory_provider = nullptr;
    IBootControlAdapter* boot_control_adapter = nullptr;
    const IRollbackTokenFactory* rollback_token_factory = nullptr;
  };

  explicit SlotSwitchCoordinator(Dependencies dependencies);

  [[nodiscard]] SlotSelectionResult select_inactive_slot(
      std::string_view slot_group) const;

  [[nodiscard]] SlotSwitchPreparationResult build_slot_plan(
      std::string_view slot_group,
      const std::vector<InstallEvidence>& staged_evidence,
      const SwitchPolicySnapshot& policy) const;

  [[nodiscard]] BootSwitchResult set_next_boot(const SlotPlan& slot_plan);

 private:
  Dependencies dependencies_;
};

}  // namespace dasall::infra::ota