#pragma once

#include <string_view>

#include "error/ResultCode.h"

namespace dasall::infra::tracing {

enum class TraceErrorCode {
  ProviderNotReady = 1,
  InvalidContext = 2,
  QueueFull = 3,
  ExportTimeout = 4,
  ExportFailure = 5,
  ShutdownTimeout = 6,
  ConfigInvalid = 7,
};

struct TraceErrorMapping {
  TraceErrorCode trace_code;
  contracts::ResultCode result_code;
  std::string_view source_anchor;
  std::string_view reason;
};

inline constexpr std::string_view trace_error_code_name(TraceErrorCode code) {
  switch (code) {
    case TraceErrorCode::ProviderNotReady:
      return "TRC_E_PROVIDER_NOT_READY";
    case TraceErrorCode::InvalidContext:
      return "TRC_E_INVALID_CONTEXT";
    case TraceErrorCode::QueueFull:
      return "TRC_E_QUEUE_FULL";
    case TraceErrorCode::ExportTimeout:
      return "TRC_E_EXPORT_TIMEOUT";
    case TraceErrorCode::ExportFailure:
      return "TRC_E_EXPORT_FAILURE";
    case TraceErrorCode::ShutdownTimeout:
      return "TRC_E_SHUTDOWN_TIMEOUT";
    case TraceErrorCode::ConfigInvalid:
      return "TRC_E_CONFIG_INVALID";
  }

  return "TRC_E_UNKNOWN";
}

inline constexpr TraceErrorMapping map_trace_error_code(TraceErrorCode code) {
  switch (code) {
    case TraceErrorCode::ProviderNotReady:
      return TraceErrorMapping{
          .trace_code = code,
          .result_code = contracts::ResultCode::ProviderTimeout,
          .source_anchor = "6.6 ITracerProvider",
          .reason = "tracing provider not-ready states stay inside the contracts provider failure category until init succeeds",
      };
    case TraceErrorCode::InvalidContext:
      return TraceErrorMapping{
          .trace_code = code,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .source_anchor = "6.3 ContextPropagationAdapter",
          .reason = "invalid or malformed trace context inputs stay inside the contracts validation failure category",
      };
    case TraceErrorCode::QueueFull:
      return TraceErrorMapping{
          .trace_code = code,
          .result_code = contracts::ResultCode::RuntimeRetryExhausted,
          .source_anchor = "6.8 queue exception",
          .reason = "trace batch queue saturation stays inside the contracts runtime failure category",
      };
    case TraceErrorCode::ExportTimeout:
      return TraceErrorMapping{
          .trace_code = code,
          .result_code = contracts::ResultCode::ProviderTimeout,
          .source_anchor = "6.6 ISpanExporter",
          .reason = "trace exporter timeout stays inside the contracts provider timeout category",
      };
    case TraceErrorCode::ExportFailure:
      return TraceErrorMapping{
          .trace_code = code,
          .result_code = contracts::ResultCode::ProviderTimeout,
          .source_anchor = "6.8 export exception",
          .reason = "trace exporter dependency failures stay inside the contracts provider failure category",
      };
    case TraceErrorCode::ShutdownTimeout:
      return TraceErrorMapping{
          .trace_code = code,
          .result_code = contracts::ResultCode::RuntimeRetryExhausted,
          .source_anchor = "6.8 shutdown exception",
          .reason = "trace shutdown timeout stays inside the contracts runtime failure category",
      };
    case TraceErrorCode::ConfigInvalid:
      return TraceErrorMapping{
          .trace_code = code,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .source_anchor = "6.6 ITracerProvider",
          .reason = "tracing config validation failures stay inside the contracts validation failure category",
      };
  }

  return TraceErrorMapping{
      .trace_code = code,
      .result_code = contracts::ResultCode::RuntimeRetryExhausted,
      .source_anchor = "unknown",
      .reason = "unknown tracing errors fall back to the contracts runtime failure category",
  };
}

}  // namespace dasall::infra::tracing