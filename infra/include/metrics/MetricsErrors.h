#pragma once

#include <string_view>

#include "error/ResultCode.h"

namespace dasall::infra::metrics {

enum class MetricsErrorCode {
  ProviderNotReady = 1,
  IdentityInvalid = 2,
  LabelCardinalityExceeded = 3,
  QueueFull = 4,
  ExportFailure = 5,
  ExportTimeout = 6,
  ConfigInvalid = 7,
};

struct MetricsErrorMapping {
  MetricsErrorCode metrics_code;
  contracts::ResultCode result_code;
  std::string_view source_anchor;
  std::string_view reason;
};

inline constexpr std::string_view metrics_error_code_name(MetricsErrorCode code) {
  switch (code) {
    case MetricsErrorCode::ProviderNotReady:
      return "MET_E_PROVIDER_NOT_READY";
    case MetricsErrorCode::IdentityInvalid:
      return "MET_E_IDENTITY_INVALID";
    case MetricsErrorCode::LabelCardinalityExceeded:
      return "MET_E_LABEL_CARDINALITY_EXCEEDED";
    case MetricsErrorCode::QueueFull:
      return "MET_E_QUEUE_FULL";
    case MetricsErrorCode::ExportFailure:
      return "MET_E_EXPORT_FAILURE";
    case MetricsErrorCode::ExportTimeout:
      return "MET_E_EXPORT_TIMEOUT";
    case MetricsErrorCode::ConfigInvalid:
      return "MET_E_CONFIG_INVALID";
  }

  return "MET_E_UNKNOWN";
}

inline constexpr MetricsErrorMapping map_metrics_error_code(MetricsErrorCode code) {
  switch (code) {
    case MetricsErrorCode::ProviderNotReady:
      return MetricsErrorMapping{
          .metrics_code = code,
          .result_code = contracts::ResultCode::ProviderTimeout,
          .source_anchor = "6.6 IMetricsProvider",
          .reason = "metrics provider not-ready states stay inside the contracts provider failure category until init succeeds",
      };
    case MetricsErrorCode::IdentityInvalid:
      return MetricsErrorMapping{
          .metrics_code = code,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .source_anchor = "6.5 MetricIdentity",
          .reason = "invalid metric identity inputs stay inside the contracts validation failure category",
      };
    case MetricsErrorCode::LabelCardinalityExceeded:
      return MetricsErrorMapping{
          .metrics_code = code,
          .result_code = contracts::ResultCode::PolicyDenied,
          .source_anchor = "6.8 label exception",
          .reason = "label allowlist and cardinality violations stay inside the contracts policy failure category",
      };
    case MetricsErrorCode::QueueFull:
      return MetricsErrorMapping{
          .metrics_code = code,
          .result_code = contracts::ResultCode::RuntimeRetryExhausted,
          .source_anchor = "6.8 queue exception",
          .reason = "metrics queue saturation stays inside the contracts runtime failure category",
      };
    case MetricsErrorCode::ExportFailure:
      return MetricsErrorMapping{
          .metrics_code = code,
          .result_code = contracts::ResultCode::ProviderTimeout,
          .source_anchor = "6.8 export exception",
          .reason = "metrics exporter dependency failures stay inside the contracts provider failure category",
      };
    case MetricsErrorCode::ExportTimeout:
      return MetricsErrorMapping{
          .metrics_code = code,
          .result_code = contracts::ResultCode::ProviderTimeout,
          .source_anchor = "6.6 IMetricExporter",
          .reason = "metrics exporter timeout stays inside the contracts provider timeout category",
      };
    case MetricsErrorCode::ConfigInvalid:
      return MetricsErrorMapping{
          .metrics_code = code,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .source_anchor = "6.9 metrics config",
          .reason = "metrics config validation failures stay inside the contracts validation failure category",
      };
  }

  return MetricsErrorMapping{
      .metrics_code = code,
      .result_code = contracts::ResultCode::RuntimeRetryExhausted,
      .source_anchor = "unknown",
      .reason = "unknown metrics errors fall back to the contracts runtime failure category",
  };
}

}  // namespace dasall::infra::metrics