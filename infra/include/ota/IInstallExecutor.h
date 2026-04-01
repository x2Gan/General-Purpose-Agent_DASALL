#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "ota/OTATypes.h"

namespace dasall::infra::ota {

struct StageArtifactResult {
  bool staged = false;
  InstallEvidence evidence;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static StageArtifactResult success(InstallEvidence evidence) {
    return StageArtifactResult{
        .staged = true,
        .evidence = std::move(evidence),
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static StageArtifactResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return StageArtifactResult{
        .staged = false,
        .evidence = {},
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
      return staged && evidence.is_valid();
    }

    return error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(result_code);
  }
};

struct BootSwitchResult {
  bool switched = false;
  std::string target_slot;
  bool reboot_required = false;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static BootSwitchResult success(std::string target_slot,
                                                bool reboot_required = true) {
    return BootSwitchResult{
        .switched = true,
        .target_slot = std::move(target_slot),
        .reboot_required = reboot_required,
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static BootSwitchResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return BootSwitchResult{
        .switched = false,
        .target_slot = {},
        .reboot_required = false,
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
      return switched && !target_slot.empty();
    }

    return error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(result_code);
  }
};

struct RollbackResult {
  bool rolled_back = false;
  std::string restored_target;
  std::string evidence_ref;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static RollbackResult success(std::string restored_target,
                                              std::string evidence_ref) {
    return RollbackResult{
        .rolled_back = true,
        .restored_target = std::move(restored_target),
        .evidence_ref = std::move(evidence_ref),
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static RollbackResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return RollbackResult{
        .rolled_back = false,
        .restored_target = {},
        .evidence_ref = {},
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
      return rolled_back && !restored_target.empty() && !evidence_ref.empty();
    }

    return error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(result_code);
  }
};

class IInstallExecutor {
 public:
  virtual ~IInstallExecutor() = default;

  virtual StageArtifactResult stage_artifact(const ArtifactDescriptor& artifact_descriptor,
                                             std::string_view target) = 0;
  virtual BootSwitchResult activate_plan(const SlotPlan& slot_plan) = 0;
  virtual RollbackResult revert_install(const RollbackToken& rollback_token) = 0;
};

}  // namespace dasall::infra::ota