#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "config/ConfigTypes.h"
#include "error/ErrorInfo.h"
#include "error/ResultCode.h"

namespace dasall::infra::config {

struct ConfigSnapshotCommitResult {
  bool committed = false;
  std::uint64_t current_version = 0;
  std::uint64_t last_known_good_version = 0;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static ConfigSnapshotCommitResult success(std::uint64_t current_version,
                                                          std::uint64_t last_known_good_version) {
    return ConfigSnapshotCommitResult{
        .committed = true,
        .current_version = current_version,
        .last_known_good_version = last_known_good_version,
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static ConfigSnapshotCommitResult failure(contracts::ResultCode result_code,
                                                          std::string message,
                                                          std::string stage,
                                                          std::string source_ref) {
    return ConfigSnapshotCommitResult{
        .committed = false,
        .current_version = 0,
        .last_known_good_version = 0,
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
      return committed;
    }

    return error_info->failure_type.has_value() &&
           *error_info->failure_type == contracts::classify_result_code(result_code);
  }
};

class IConfigSnapshotStore {
 public:
  virtual ~IConfigSnapshotStore() = default;

  virtual ConfigSnapshotCommitResult commit(const ConfigSnapshot& snapshot) = 0;
  [[nodiscard]] virtual std::optional<ConfigSnapshot> get_current() const = 0;
  [[nodiscard]] virtual std::optional<ConfigSnapshot> get_by_version(std::uint64_t version) const = 0;
  [[nodiscard]] virtual std::optional<ConfigSnapshot> get_last_known_good() const = 0;
};

}  // namespace dasall::infra::config