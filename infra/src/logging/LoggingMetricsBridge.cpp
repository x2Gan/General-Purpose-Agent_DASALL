#include "LoggingMetricsBridge.h"

#include <array>
#include <string>
#include <utility>

namespace dasall::infra::logging {

namespace {

constexpr std::string_view kLoggingMetricsBridgeSourceRef =
    "LoggingMetricsBridge";
constexpr std::string_view kLoggingMetricsBridgeStage =
    "logging.metrics_bridge";

constexpr std::array<LoggingMetricKind, kLoggingMetricFamilyCount>
    kLoggingMetricKinds{
        LoggingMetricKind::WriteTotal,
        LoggingMetricKind::WriteFailTotal,
        LoggingMetricKind::DropTotal,
        LoggingMetricKind::QueueDepth,
        LoggingMetricKind::FlushLatencyMs,
    };

std::string normalize_profile_id(std::string profile_id) {
  if (profile_id.empty()) {
    return std::string("unknown");
  }

  return profile_id;
}

std::string logging_metric_description(LoggingMetricKind kind) {
  switch (kind) {
    case LoggingMetricKind::WriteTotal:
      return "logging successful writes";
    case LoggingMetricKind::WriteFailTotal:
      return "logging failed writes";
    case LoggingMetricKind::DropTotal:
      return "logging dropped records";
    case LoggingMetricKind::QueueDepth:
      return "logging async queue depth";
    case LoggingMetricKind::FlushLatencyMs:
      return "logging flush latency in milliseconds";
  }

  return "logging bridge metric";
}

metrics::MetricsOperationStatus make_metrics_failure_status(
    metrics::MetricsErrorCode error_code,
    std::string message) {
  const auto mapping = metrics::map_metrics_error_code(error_code);
  return metrics::MetricsOperationStatus::failure(
      mapping.result_code,
      std::move(message),
      std::string(kLoggingMetricsBridgeStage),
      std::string(kLoggingMetricsBridgeSourceRef));
}

bool metrics_error_causes_degraded(metrics::MetricsErrorCode error_code) {
  switch (error_code) {
    case metrics::MetricsErrorCode::ProviderNotReady:
    case metrics::MetricsErrorCode::IdentityInvalid:
    case metrics::MetricsErrorCode::ExportFailure:
    case metrics::MetricsErrorCode::ExportTimeout:
    case metrics::MetricsErrorCode::ConfigInvalid:
      return true;
    case metrics::MetricsErrorCode::LabelCardinalityExceeded:
    case metrics::MetricsErrorCode::QueueFull:
      return false;
  }

  return true;
}

metrics::MetricsErrorCode infer_metrics_error_code(
    const metrics::MetricsOperationStatus& status,
    metrics::MetricsErrorCode fallback) {
  switch (status.result_code) {
    case contracts::ResultCode::ProviderTimeout:
      return metrics::MetricsErrorCode::ExportFailure;
    case contracts::ResultCode::PolicyDenied:
      return metrics::MetricsErrorCode::LabelCardinalityExceeded;
    case contracts::ResultCode::ValidationFieldMissing:
      return metrics::MetricsErrorCode::ConfigInvalid;
    case contracts::ResultCode::RuntimeRetryExhausted:
      return metrics::MetricsErrorCode::QueueFull;
    default:
      return fallback;
  }
}

std::string logging_error_code_label(const LoggingMetricSignal& signal) {
  if (!signal.logging_error_code.has_value()) {
    return std::string(kLoggingMetricNoErrorCodeLabel);
  }

  return std::string(logging_error_code_name(*signal.logging_error_code));
}

}  // namespace

metrics::MetricIdentity make_logging_metric_identity(LoggingMetricKind kind) {
  return metrics::MetricIdentity{
      .name = std::string(logging_metric_name(kind)),
      .type = logging_metric_type(kind),
      .unit = std::string(logging_metric_unit(kind)),
      .description = logging_metric_description(kind),
  };
}

LoggingMetricsBridge::LoggingMetricsBridge(
    std::shared_ptr<metrics::IMetricsProvider> metrics_provider,
    std::string profile_id)
    : metrics_provider_(std::move(metrics_provider)),
      profile_id_(normalize_profile_id(std::move(profile_id))) {}

LoggingMetricsEmitResult LoggingMetricsBridge::emit(
    const LoggingMetricSignal& signal) {
  ++emission_attempt_total_;

  if (!signal.has_consistent_values()) {
    return make_failure_result(
        metrics::MetricsErrorCode::ConfigInvalid,
        make_metrics_failure_status(
            metrics::MetricsErrorCode::ConfigInvalid,
            "logging metric signal violates the frozen stage/outcome/value rules"));
  }

  const auto sample = make_sample(signal);
  if (!sample.is_valid() ||
      !is_logging_metric_error_code(sample.labels.error_code)) {
    return make_failure_result(
        metrics::MetricsErrorCode::ConfigInvalid,
        make_metrics_failure_status(
            metrics::MetricsErrorCode::ConfigInvalid,
            "logging metric sample violates the frozen identity or label contract"));
  }

  LoggingMetricsEmitResult failure;
  if (!ensure_meter_ready(&failure)) {
    return failure;
  }

  if (!ensure_instruments_registered(&failure)) {
    return failure;
  }

  const auto status = meter_->record(sample);
  if (!status.ok) {
    return make_failure_result(
        infer_metrics_error_code(status,
                                 metrics::MetricsErrorCode::ExportFailure),
        status);
  }

  degraded_ = false;
  last_metrics_error_code_.reset();
  return LoggingMetricsEmitResult{
      .emitted = true,
      .bridge_degraded = false,
      .status = status,
      .metrics_error_code = std::nullopt,
  };
}

bool LoggingMetricsBridge::ensure_meter_ready(
    LoggingMetricsEmitResult* failure) {
  if (meter_) {
    return true;
  }

  if (!metrics_provider_) {
    *failure = make_failure_result(
        metrics::MetricsErrorCode::ProviderNotReady,
        make_metrics_failure_status(
            metrics::MetricsErrorCode::ProviderNotReady,
            "logging metrics bridge requires a metrics provider before emitting samples"));
    return false;
  }

  auto meter = metrics_provider_->get_meter(metrics::MeterScope{
      .name = std::string(kLoggingMetricsMeterScopeName),
      .version = std::string(kLoggingMetricsMeterScopeVersion),
      .schema_url = std::string(),
  });
  if (!meter) {
    *failure = make_failure_result(
        metrics::MetricsErrorCode::ProviderNotReady,
        make_metrics_failure_status(
            metrics::MetricsErrorCode::ProviderNotReady,
            "metrics provider did not supply the infra.logging meter scope"));
    return false;
  }

  meter_ = std::move(meter);
  return true;
}

bool LoggingMetricsBridge::ensure_instruments_registered(
    LoggingMetricsEmitResult* failure) {
  if (instruments_registered_) {
    return true;
  }

  instrument_handles_.fill(std::nullopt);
  for (const auto kind : kLoggingMetricKinds) {
    const auto identity = make_logging_metric_identity(kind);
    std::optional<metrics::InstrumentHandle> handle;

    switch (logging_metric_type(kind)) {
      case metrics::MetricType::Counter:
        handle = meter_->create_counter(identity);
        break;
      case metrics::MetricType::Gauge:
        handle = meter_->create_gauge(identity);
        break;
      case metrics::MetricType::Histogram:
        handle = meter_->create_histogram(identity);
        break;
      case metrics::MetricType::UpDownCounter:
        handle = std::nullopt;
        break;
    }

    if (!handle.has_value() || !handle->is_valid()) {
      instrument_handles_.fill(std::nullopt);
      instruments_registered_ = false;
      meter_.reset();
      *failure = make_failure_result(
          metrics::MetricsErrorCode::IdentityInvalid,
          make_metrics_failure_status(
              metrics::MetricsErrorCode::IdentityInvalid,
              "logging metrics bridge could not register metric family " +
                  identity.name));
      return false;
    }

    instrument_handles_[to_index(kind)] = std::move(handle);
  }

  instruments_registered_ = true;
  return true;
}

LoggingMetricsEmitResult LoggingMetricsBridge::make_failure_result(
    metrics::MetricsErrorCode error_code,
    metrics::MetricsOperationStatus status) {
  ++emission_failure_total_;
  last_metrics_error_code_ = error_code;

  const bool failure_causes_degraded = metrics_error_causes_degraded(error_code);
  degraded_ = degraded_ || failure_causes_degraded;

  if (error_code == metrics::MetricsErrorCode::ProviderNotReady ||
      error_code == metrics::MetricsErrorCode::IdentityInvalid ||
      error_code == metrics::MetricsErrorCode::ConfigInvalid) {
    meter_.reset();
    instrument_handles_.fill(std::nullopt);
    instruments_registered_ = false;
  }

  return LoggingMetricsEmitResult{
      .emitted = false,
      .bridge_degraded = failure_causes_degraded,
      .status = std::move(status),
      .metrics_error_code = error_code,
  };
}

metrics::MetricSample LoggingMetricsBridge::make_sample(
    const LoggingMetricSignal& signal) const {
  return metrics::MetricSample{
      .identity_ref = make_logging_metric_identity(signal.kind),
      .value = signal.value,
      .ts_unix_ms = signal.ts_unix_ms,
      .labels = metrics::MetricLabels{
          .module = std::string(kLoggingMetricModuleLabel),
          .stage = signal.stage,
          .profile = profile_id_,
          .outcome = signal.outcome,
          .error_code = logging_error_code_label(signal),
      },
  };
}

}  // namespace dasall::infra::logging