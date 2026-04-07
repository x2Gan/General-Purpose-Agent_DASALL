#include "tracing/TraceMetricsBridge.h"

#include <array>
#include <string>
#include <utility>

namespace dasall::infra::tracing {
namespace {

constexpr std::string_view kTraceMetricsBridgeSourceRef = "TraceMetricsBridge";
constexpr std::string_view kTraceMetricsBridgeStage = "tracing.metrics_bridge";

constexpr std::array<TraceMetricKind, kTraceMetricFamilyCount> kTraceMetricKinds{
    TraceMetricKind::SpanStartedTotal,
    TraceMetricKind::SpanEndedTotal,
    TraceMetricKind::SpanDroppedTotal,
    TraceMetricKind::ExportSuccessTotal,
    TraceMetricKind::ExportFailureTotal,
    TraceMetricKind::ExportLatencyMs,
    TraceMetricKind::BatchQueueDepth,
    TraceMetricKind::ContextInvalidTotal,
};

std::string normalize_profile_id(std::string profile_id) {
  if (profile_id.empty()) {
    return std::string("unknown");
  }

  return profile_id;
}

std::string trace_metric_description(TraceMetricKind kind) {
  switch (kind) {
    case TraceMetricKind::SpanStartedTotal:
      return "tracing started spans";
    case TraceMetricKind::SpanEndedTotal:
      return "tracing ended spans";
    case TraceMetricKind::SpanDroppedTotal:
      return "tracing dropped spans";
    case TraceMetricKind::ExportSuccessTotal:
      return "tracing successful exports";
    case TraceMetricKind::ExportFailureTotal:
      return "tracing failed exports";
    case TraceMetricKind::ExportLatencyMs:
      return "tracing export latency in milliseconds";
    case TraceMetricKind::BatchQueueDepth:
      return "tracing batch queue depth";
    case TraceMetricKind::ContextInvalidTotal:
      return "invalid tracing contexts";
  }

  return "tracing bridge metric";
}

metrics::MetricsOperationStatus make_metrics_failure_status(
    metrics::MetricsErrorCode error_code,
    std::string message) {
  const auto mapping = metrics::map_metrics_error_code(error_code);
  return metrics::MetricsOperationStatus::failure(
      mapping.result_code,
      std::move(message),
      std::string(kTraceMetricsBridgeStage),
      std::string(kTraceMetricsBridgeSourceRef));
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

std::string trace_error_code_label(const TraceMetricSignal& signal) {
  if (!signal.trace_error_code.has_value()) {
    return std::string(kTraceMetricNoErrorCodeLabel);
  }

  return std::string(trace_error_code_name(*signal.trace_error_code));
}

}  // namespace

metrics::MetricIdentity make_trace_metric_identity(TraceMetricKind kind) {
  return metrics::MetricIdentity{
      .name = std::string(trace_metric_name(kind)),
      .type = trace_metric_type(kind),
      .unit = std::string(trace_metric_unit(kind)),
      .description = trace_metric_description(kind),
  };
}

TraceMetricsBridge::TraceMetricsBridge(
    std::shared_ptr<metrics::IMetricsProvider> metrics_provider,
    std::string profile_id)
    : metrics_provider_(std::move(metrics_provider)),
      profile_id_(normalize_profile_id(std::move(profile_id))) {}

void TraceMetricsBridge::set_metrics_provider(
    std::shared_ptr<metrics::IMetricsProvider> metrics_provider,
    std::string profile_id) {
  metrics_provider_ = std::move(metrics_provider);
  profile_id_ = normalize_profile_id(std::move(profile_id));
  meter_.reset();
  instrument_handles_.fill(std::nullopt);
  instruments_registered_ = false;
  degraded_ = false;
  last_metrics_error_code_.reset();
}

TraceMetricsEmitResult TraceMetricsBridge::emit(const TraceMetricSignal& signal) {
  ++emission_attempt_total_;

  if (!signal.has_consistent_values()) {
    return make_failure_result(
        metrics::MetricsErrorCode::ConfigInvalid,
        make_metrics_failure_status(
            metrics::MetricsErrorCode::ConfigInvalid,
            "tracing metric signal violates the frozen stage/outcome/value rules"));
  }

  const auto sample = make_sample(signal);
  if (!sample.is_valid() || !is_trace_metric_error_code(sample.labels.error_code)) {
    return make_failure_result(
        metrics::MetricsErrorCode::ConfigInvalid,
        make_metrics_failure_status(
            metrics::MetricsErrorCode::ConfigInvalid,
            "tracing metric sample violates the frozen identity or label contract"));
  }

  TraceMetricsEmitResult failure;
  if (!ensure_meter_ready(&failure)) {
    return failure;
  }

  if (!ensure_instruments_registered(&failure)) {
    return failure;
  }

  const auto status = meter_->record(sample);
  if (!status.ok) {
    return make_failure_result(
        infer_metrics_error_code(status, metrics::MetricsErrorCode::ExportFailure),
        status);
  }

  degraded_ = false;
  last_metrics_error_code_.reset();
  return TraceMetricsEmitResult{
      .emitted = true,
      .bridge_degraded = false,
      .status = status,
      .metrics_error_code = std::nullopt,
  };
}

bool TraceMetricsBridge::ensure_meter_ready(TraceMetricsEmitResult* failure) {
  if (meter_) {
    return true;
  }

  if (!metrics_provider_) {
    *failure = make_failure_result(
        metrics::MetricsErrorCode::ProviderNotReady,
        make_metrics_failure_status(
            metrics::MetricsErrorCode::ProviderNotReady,
            "tracing metrics bridge requires a metrics provider before emitting samples"));
    return false;
  }

  auto meter = metrics_provider_->get_meter(metrics::MeterScope{
      .name = std::string(kTraceMetricsMeterScopeName),
      .version = std::string(kTraceMetricsMeterScopeVersion),
      .schema_url = std::string(),
  });
  if (!meter) {
    *failure = make_failure_result(
        metrics::MetricsErrorCode::ProviderNotReady,
        make_metrics_failure_status(
            metrics::MetricsErrorCode::ProviderNotReady,
            "metrics provider did not supply the infra.tracing meter scope"));
    return false;
  }

  meter_ = std::move(meter);
  return true;
}

bool TraceMetricsBridge::ensure_instruments_registered(
    TraceMetricsEmitResult* failure) {
  if (instruments_registered_) {
    return true;
  }

  instrument_handles_.fill(std::nullopt);
  for (const auto kind : kTraceMetricKinds) {
    const auto identity = make_trace_metric_identity(kind);
    std::optional<metrics::InstrumentHandle> handle;

    switch (trace_metric_type(kind)) {
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
              "tracing metrics bridge could not register metric family " +
                  identity.name));
      return false;
    }

    instrument_handles_[to_index(kind)] = std::move(handle);
  }

  instruments_registered_ = true;
  return true;
}

TraceMetricsEmitResult TraceMetricsBridge::make_failure_result(
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

  return TraceMetricsEmitResult{
      .emitted = false,
      .bridge_degraded = failure_causes_degraded,
      .status = std::move(status),
      .metrics_error_code = error_code,
  };
}

metrics::MetricSample TraceMetricsBridge::make_sample(
    const TraceMetricSignal& signal) const {
  return metrics::MetricSample{
      .identity_ref = make_trace_metric_identity(signal.kind),
      .value = signal.value,
      .ts_unix_ms = signal.ts_unix_ms,
      .labels = metrics::MetricLabels{
          .module = std::string(kTraceMetricModuleLabel),
          .stage = signal.stage,
          .profile = profile_id_,
          .outcome = signal.outcome,
          .error_code = trace_error_code_label(signal),
      },
  };
}

}  // namespace dasall::infra::tracing