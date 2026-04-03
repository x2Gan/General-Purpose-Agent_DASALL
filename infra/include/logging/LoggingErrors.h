#pragma once

#include <string_view>

#include "error/ResultCode.h"

namespace dasall::infra::logging {

enum class LoggingErrorCode {
  QueueFull = 1,
  SinkIo = 2,
  FormatInvalid = 3,
  ConfigInvalid = 4,
};

struct LoggingErrorMapping {
  LoggingErrorCode logging_code;
  contracts::ResultCode result_code;
  std::string_view source_anchor;
  std::string_view reason;
};

inline constexpr std::string_view logging_error_code_name(LoggingErrorCode code) {
  switch (code) {
    case LoggingErrorCode::QueueFull:
      return "LOG_E_QUEUE_FULL";
    case LoggingErrorCode::SinkIo:
      return "LOG_E_SINK_IO";
    case LoggingErrorCode::FormatInvalid:
      return "LOG_E_FORMAT_INVALID";
    case LoggingErrorCode::ConfigInvalid:
      return "LOG_E_CONFIG_INVALID";
  }

  return "LOG_E_UNKNOWN";
}

inline constexpr LoggingErrorMapping map_logging_error_code(LoggingErrorCode code) {
  switch (code) {
    case LoggingErrorCode::QueueFull:
      return LoggingErrorMapping{
          .logging_code = code,
          .result_code = contracts::ResultCode::RuntimeRetryExhausted,
          .source_anchor = "6.8 queue full",
          .reason = "logging queue saturation stays inside the contracts runtime failure category until backpressure is relieved",
      };
    case LoggingErrorCode::SinkIo:
      return LoggingErrorMapping{
          .logging_code = code,
          .result_code = contracts::ResultCode::ProviderTimeout,
          .source_anchor = "6.8 sink IO failure",
          .reason = "logging sink IO dependency failures stay inside the contracts provider failure category while recovery switches to fallback sink",
      };
    case LoggingErrorCode::FormatInvalid:
      return LoggingErrorMapping{
          .logging_code = code,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .source_anchor = "6.8 format failure",
          .reason = "logging formatter failures stay inside the contracts validation failure category because the payload can no longer satisfy the structured record contract",
      };
    case LoggingErrorCode::ConfigInvalid:
      return LoggingErrorMapping{
          .logging_code = code,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .source_anchor = "6.6 ILogConfigurator",
          .reason = "invalid logging configuration stays inside the contracts validation failure category until the merged config is corrected",
      };
  }

  return LoggingErrorMapping{
      .logging_code = code,
      .result_code = contracts::ResultCode::RuntimeRetryExhausted,
      .source_anchor = "unknown",
      .reason = "unknown logging errors fall back to the contracts runtime failure category",
  };
}

}  // namespace dasall::infra::logging