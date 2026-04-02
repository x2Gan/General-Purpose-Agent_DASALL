#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"

namespace dasall::infra::watchdog {

struct WatchedEntityDescriptor;

struct HeartbeatSourceEmissionResult {
  bool ok = false;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static HeartbeatSourceEmissionResult success() {
    return HeartbeatSourceEmissionResult{
        .ok = true,
        .result_code = std::nullopt,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static HeartbeatSourceEmissionResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return HeartbeatSourceEmissionResult{
        .ok = false,
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
                .ref_type = "infra.watchdog",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error.has_value()) {
      return ok && !result_code.has_value();
    }

    return result_code.has_value() && error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(*result_code);
  }
};

struct HeartbeatSourceDescribeResult {
  bool ok = false;
  std::shared_ptr<const WatchedEntityDescriptor> descriptor;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static HeartbeatSourceDescribeResult success(
      std::shared_ptr<const WatchedEntityDescriptor> descriptor) {
    return HeartbeatSourceDescribeResult{
        .ok = true,
        .descriptor = std::move(descriptor),
        .result_code = std::nullopt,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static HeartbeatSourceDescribeResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return HeartbeatSourceDescribeResult{
        .ok = false,
        .descriptor = nullptr,
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
                .ref_type = "infra.watchdog",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error.has_value()) {
      return ok && !result_code.has_value();
    }

    return result_code.has_value() && error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(*result_code);
  }

  [[nodiscard]] bool has_descriptor() const {
    return ok && descriptor != nullptr && !result_code.has_value() &&
           !error.has_value();
  }
};

class IHeartbeatSource {
 public:
  virtual ~IHeartbeatSource() = default;

  virtual HeartbeatSourceEmissionResult emit_heartbeat(
      std::string_view entity_id) = 0;
  [[nodiscard]] virtual HeartbeatSourceDescribeResult describe() const = 0;
};

}  // namespace dasall::infra::watchdog