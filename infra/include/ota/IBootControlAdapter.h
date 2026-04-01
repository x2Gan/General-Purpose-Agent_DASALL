#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "ota/OTATypes.h"

namespace dasall::infra::ota {

struct BootTargetQueryResult {
  bool resolved = false;
  std::string active_target;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static BootTargetQueryResult success(std::string active_target) {
    return BootTargetQueryResult{
        .resolved = true,
        .active_target = std::move(active_target),
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static BootTargetQueryResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return BootTargetQueryResult{
        .resolved = false,
        .active_target = {},
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
      return resolved && !active_target.empty();
    }

    return error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(result_code);
  }
};

struct BootMutationResult {
  bool applied = false;
  std::string target;
  std::string operation;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static BootMutationResult success(std::string target,
                                                  std::string operation) {
    return BootMutationResult{
        .applied = true,
        .target = std::move(target),
        .operation = std::move(operation),
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static BootMutationResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return BootMutationResult{
        .applied = false,
        .target = {},
        .operation = {},
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
      return applied && !target.empty() && !operation.empty();
    }

    return error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(result_code);
  }
};

class IBootControlAdapter {
 public:
  virtual ~IBootControlAdapter() = default;

  [[nodiscard]] virtual BootTargetQueryResult get_active_target() const = 0;
  [[nodiscard]] virtual BootMutationResult set_next_boot(std::string_view target) = 0;
  [[nodiscard]] virtual BootMutationResult mark_boot_success(std::string_view target) = 0;
  [[nodiscard]] virtual BootMutationResult mark_boot_failed(std::string_view target) = 0;
};

}  // namespace dasall::infra::ota