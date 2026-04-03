#include "AuditMetricsBridge.h"

#include <array>
#include <string>
#include <utility>

namespace dasall::infra::audit {

namespace {

constexpr std::string_view kAuditMetricsBridgeSourceRef = "AuditMetricsBridge";
constexpr std::string_view kAuditMetricsBridgeStage = "audit.metrics_bridge";

constexpr std::array<AuditMetricKind, kAuditMetricFamilyCount>
    kAuditMetricKinds{
        AuditMetricKind::WriteTotal,
        AuditMetricKind::WriteFailTotal,
        AuditMetricKind::FallbackTotal,
        AuditMetricKind::FallbackFailTotal,
        AuditMetricKind::ExportTotal,
        AuditMetricKind::ExportFailTotal,
        AuditMetricKind::QueueDepth,
    };

std::string normalize_profile_id(std::string profile_id) {
  if (profile_id.empty()) {
    return std::string("unknown");
  }

  return profile_id;
}

std::string audit_metric_description(AuditMetricKind kind) {
  switch (kind) {
    case AuditMetricKind::WriteTotal:
      return "audit successful writes";
    case AuditMetricKind::WriteFailTotal:
      return "audit failed writes";
    case AuditMetricKind::FallbackTotal:
      return "audit fallback accepted writes";
    case AuditMetricKind::FallbackFailTotal:
      return "audit fallback failures";
    case AuditMetricKind::ExportTotal:
      return "audit successful exports";
    case AuditMetricKind::ExportFailTotal:
      return "audit failed exports";
    case AuditMetricKind::QueueDepth:
      return "audit retained record depth";
  }

  return "audit bridge metric";
}

metrics::MetricsOperationStatus make_metrics_failure_status(
    metrics::MetricsErrorCode error_code,
    std::string message) {
  const auto mapping = metrics::map_metrics_error_code(error_code);
  return metrics::MetricsOperationStatus::failure(
      mapping.result_code,
      std::move(message),
      std::string(kAuditMetricsBridgeStage),
      std::string(kAuditMetricsBridgeSourceRef));
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

std::string audit_error_code_label(const AuditMetricSignal& signal) {
  if (!signal.audit_error_code.has_value()) {
    return std::string(kAuditMetricNoErrorCodeLabel);
  }

  return std::string(audit_error_code_name(*signal.audit_error_code));
}

}  // namespace

metrics::MetricIdentity make_audit_metric_identity(AuditMetricKind kind) {
  return metrics::MetricIdentity{
      .name = std::string(audit_metric_name(kind)),
      .type = audit_metric_type(kind),
      .unit = std::string(audit_metric_unit(kind)),
      .description = audit_metric_description(kind),
  };
}

AuditMetricsBridge::AuditMetricsBridge(
    std::shared_ptr<metrics::IMetricsProvider> metrics_provider,
    std::string profile_id)
    : metrics_provider_(std::move(metrics_provider)),
      profile_id_(normalize_profile_id(std::move(profile_id))) {}

AuditMetricsEmitResult AuditMetricsBridge::emit(
    const AuditMetricSignal& signal) {
  ++emission_attempt_total_;

  if (no_op_mode_) {
    return make_failure_result(
        last_metrics_error_code_.value_or(metrics::MetricsErrorCode::ConfigInvalid),
        make_metrics_failure_status(
            last_metrics_error_code_.value_or(metrics::MetricsErrorCode::ConfigInvalid),
            "audit metrics bridge is in no-op mode after a previous initialization failure"));
  }

  if (!signal.has_consistent_values()) {
    return make_failure_result(
        metrics::MetricsErrorCode::ConfigInvalid,
        make_metrics_failure_status(
            metrics::MetricsErrorCode::ConfigInvalid,
            "audit metric signal violates the frozen stage/outcome/value rules"));
  }

  const auto sample = make_sample(signal);
  if (!sample.is_valid() || !is_audit_metric_error_code(sample.labels.error_code)) {
    return make_failure_result(
        metrics::MetricsErrorCode::ConfigInvalid,
        make_metrics_failure_status(
            metrics::MetricsErrorCode::ConfigInvalid,
            "audit metric sample violates the frozen identity or label contract"));
  }

  AuditMetricsEmitResult failure;
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
  return AuditMetricsEmitResult{
      .emitted = true,
      .bridge_degraded = false,
      .status = status,
      .metrics_error_code = std::nullopt,
  };
}

bool AuditMetricsBridge::ensure_meter_ready(AuditMetricsEmitResult* failure) {
  if (meter_) {
    return true;
  }

  if (!metrics_provider_) {
    *failure = make_failure_result(
        metrics::MetricsErrorCode::ProviderNotReady,
        make_metrics_failure_status(
            metrics::MetricsErrorCode::ProviderNotReady,
            "audit metrics bridge requires a metrics provider before emitting samples"));
    return false;
  }

  auto meter = metrics_provider_->get_meter(metrics::MeterScope{
      .name = std::string(kAuditMetricsMeterScopeName),
      .version = std::string(kAuditMetricsMeterScopeVersion),
      .schema_url = std::string(),
  });
  if (!meter) {
    *failure = make_failure_result(
        metrics::MetricsErrorCode::ProviderNotReady,
        make_metrics_failure_status(
            metrics::MetricsErrorCode::ProviderNotReady,
            "metrics provider did not supply the infra.audit meter scope"));
    return false;
  }

  meter_ = std::move(meter);
  return true;
}

bool AuditMetricsBridge::ensure_instruments_registered(
    AuditMetricsEmitResult* failure) {
  if (instruments_registered_) {
    return true;
  }

  instrument_handles_.fill(std::nullopt);
  for (const auto kind : kAuditMetricKinds) {
    const auto identity = make_audit_metric_identity(kind);
    std::optional<metrics::InstrumentHandle> handle;

    switch (audit_metric_type(kind)) {
      case metrics::MetricType::Counter:
        handle = meter_->create_counter(identity);
        break;
      case metrics::MetricType::Gauge:
        handle = meter_->create_gauge(identity);
        break;
      case metrics::MetricType::Histogram:
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
              "audit metrics bridge could not register metric family " +
                  identity.name));
      return false;
    }

    instrument_handles_[to_index(kind)] = std::move(handle);
  }

  instruments_registered_ = true;
  return true;
}

AuditMetricsEmitResult AuditMetricsBridge::make_failure_result(
    metrics::MetricsErrorCode error_code,
    metrics::MetricsOperationStatus status) {
  ++emission_failure_total_;
  last_metrics_error_code_ = error_code;

  const bool failure_causes_degraded = metrics_error_causes_degraded(error_code);
  degraded_ = degraded_ || failure_causes_degraded;

  if (error_code == metrics::MetricsErrorCode::ProviderNotReady) {
    meter_.reset();
    instrument_handles_.fill(std::nullopt);
    instruments_registered_ = false;
  }

  if (error_code == metrics::MetricsErrorCode::IdentityInvalid ||
      error_code == metrics::MetricsErrorCode::ConfigInvalid) {
    meter_.reset();
    instrument_handles_.fill(std::nullopt);
    instruments_registered_ = false;
    no_op_mode_ = true;
  }

  return AuditMetricsEmitResult{
      .emitted = false,
      .bridge_degraded = failure_causes_degraded,
      .status = std::move(status),
      .metrics_error_code = error_code,
  };
}

metrics::MetricSample AuditMetricsBridge::make_sample(
    const AuditMetricSignal& signal) const {
  return metrics::MetricSample{
      .identity_ref = make_audit_metric_identity(signal.kind),
      .value = signal.value,
      .ts_unix_ms = signal.ts_unix_ms,
      .labels = metrics::MetricLabels{
          .module = std::string(kAuditMetricModuleLabel),
          .stage = signal.stage,
          .profile = profile_id_,
          .outcome = signal.outcome,
          .error_code = audit_error_code_label(signal),
      },
  };
}

}  // namespace dasall::infra::audit
