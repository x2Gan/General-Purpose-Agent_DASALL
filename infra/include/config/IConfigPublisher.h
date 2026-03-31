#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include "config/ConfigTypes.h"
#include "error/ErrorInfo.h"
#include "error/ResultCode.h"

namespace dasall::infra::config {

struct ConfigPublishResult {
  bool published = false;
  std::size_t delivered_subscriber_count = 0;
  std::string event_id;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static ConfigPublishResult success(std::string event_id,
                                                   std::size_t delivered_subscriber_count) {
    return ConfigPublishResult{
        .published = true,
        .delivered_subscriber_count = delivered_subscriber_count,
        .event_id = std::move(event_id),
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static ConfigPublishResult failure(contracts::ResultCode result_code,
                                                   std::string message,
                                                   std::string stage,
                                                   std::string source_ref) {
    return ConfigPublishResult{
        .published = false,
        .delivered_subscriber_count = 0,
        .event_id = {},
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
      return published;
    }

    return error_info->failure_type.has_value() &&
           *error_info->failure_type == contracts::classify_result_code(result_code);
  }
};

class IConfigPublisher {
 public:
  virtual ~IConfigPublisher() = default;

  virtual ConfigPublishResult publish_config_changed(const ConfigDiff& diff) = 0;
};

}  // namespace dasall::infra::config