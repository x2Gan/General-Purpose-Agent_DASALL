#pragma once

#include <optional>
#include <string>
#include <vector>

#include "config/IConfigLoader.h"

namespace dasall::infra::config {

struct ConfigMergeResult {
  bool merged = false;
  ConfigSnapshot snapshot;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static ConfigMergeResult success(ConfigSnapshot snapshot) {
    return ConfigMergeResult{
        .merged = true,
        .snapshot = std::move(snapshot),
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static ConfigMergeResult failure(contracts::ResultCode result_code,
                                                 std::string message,
                                                 std::string stage,
                                                 std::string source_ref) {
    return ConfigMergeResult{
        .merged = false,
        .snapshot = {},
        .result_code = result_code,
        .error_info = contracts::ErrorInfo{
            .failure_type = contracts::classify_result_code(result_code),
            .retryable = false,
            .safe_to_replan = false,
            .details = contracts::ErrorDetails{
                .code = static_cast<int>(result_code),
                .message = std::move(message),
                .stage = std::move(stage),
            },
            .source_ref = contracts::ErrorSourceRefMinimal{
                .ref_type = "infra.config",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error_info.has_value()) {
      return merged;
    }

    return error_info->failure_type.has_value() &&
           *error_info->failure_type == contracts::classify_result_code(result_code);
  }
};

class ConfigMerger final {
 public:
  [[nodiscard]] ConfigMergeResult merge(const std::vector<ConfigLayerDocument>& layers) const;
};

}  // namespace dasall::infra::config