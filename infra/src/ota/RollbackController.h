#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ota/IBootControlAdapter.h"
#include "ota/IInstallExecutor.h"

namespace dasall::infra::ota {

struct RepoPointerRecoveryResult {
  bool recovered = false;
  std::vector<std::string> restored_refs;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static RepoPointerRecoveryResult success(
      std::vector<std::string> restored_refs) {
    return RepoPointerRecoveryResult{
        .recovered = true,
        .restored_refs = std::move(restored_refs),
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static RepoPointerRecoveryResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return RepoPointerRecoveryResult{
        .recovered = false,
        .restored_refs = {},
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
      return recovered && has_unique_non_empty_values(restored_refs);
    }

    return error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(result_code);
  }
};

struct RollbackEvidenceResult {
  bool recorded = false;
  std::string evidence_ref;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static RollbackEvidenceResult success(std::string evidence_ref) {
    return RollbackEvidenceResult{
        .recorded = true,
        .evidence_ref = std::move(evidence_ref),
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static RollbackEvidenceResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return RollbackEvidenceResult{
        .recorded = false,
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
      return recorded && !evidence_ref.empty();
    }

    return error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(result_code);
  }
};

class IRepoPointerRecoveryAdapter {
 public:
  virtual ~IRepoPointerRecoveryAdapter() = default;

  [[nodiscard]] virtual RepoPointerRecoveryResult recover(
      const RollbackToken& rollback_token,
      const std::vector<InstallEvidence>& install_evidence) const = 0;
};

class IRollbackEvidenceWriter {
 public:
  virtual ~IRollbackEvidenceWriter() = default;

  [[nodiscard]] virtual RollbackEvidenceResult write(
      const RollbackToken& rollback_token,
      std::string_view restored_target,
      const std::vector<std::string>& restored_repo_refs) const = 0;
};

class ITimeProvider {
 public:
  virtual ~ITimeProvider() = default;

  [[nodiscard]] virtual std::string now_utc() const = 0;
};

class RollbackController {
 public:
  struct Dependencies {
    IBootControlAdapter* boot_control_adapter = nullptr;
    const IRepoPointerRecoveryAdapter* repo_pointer_recovery_adapter = nullptr;
    const IRollbackEvidenceWriter* evidence_writer = nullptr;
    const ITimeProvider* time_provider = nullptr;
  };

  explicit RollbackController(Dependencies dependencies);

  [[nodiscard]] RollbackResult rollback(
      const RollbackToken& rollback_token,
      const std::vector<InstallEvidence>& install_evidence);

  [[nodiscard]] BootMutationResult restore_boot_target(
      std::string_view previous_boot_target);

  [[nodiscard]] RepoPointerRecoveryResult recover_repo_pointer(
      const RollbackToken& rollback_token,
      const std::vector<InstallEvidence>& install_evidence) const;

 private:
  Dependencies dependencies_;
};

}  // namespace dasall::infra::ota