#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "LogEvent.h"
#include "error/ErrorInfo.h"
#include "error/ResultCode.h"

namespace dasall::infra::logging {

using LogEvent = ::dasall::infra::LogEvent;
using LogLevel = ::dasall::infra::LogLevel;

struct LogFlushDeadline {
  std::uint32_t timeout_ms = 0;

  [[nodiscard]] bool is_valid() const {
    return timeout_ms > 0;
  }
};

struct LogWriteResult {
  bool ok = false;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static LogWriteResult success() {
    return LogWriteResult{
        .ok = true,
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static LogWriteResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return LogWriteResult{
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
                .ref_type = "infra.logging",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error.has_value()) {
      return ok;
    }

    return error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(result_code);
  }
};

class ILogger {
 public:
  virtual ~ILogger() = default;

  virtual LogWriteResult log(const LogEvent& event) = 0;
  virtual LogWriteResult flush(const LogFlushDeadline& deadline) = 0;
  virtual void set_level(LogLevel level) = 0;
};

}  // namespace dasall::infra::logging