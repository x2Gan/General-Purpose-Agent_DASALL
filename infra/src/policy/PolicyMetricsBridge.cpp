#include "PolicyMetricsBridge.h"

#include <array>
#include <string>
#include <utility>

namespace dasall::infra::policy {

namespace {

constexpr std::string_view kPolicyMetricsBridgeSourceRef =
    "PolicyMetricsBridge";
constexpr std::string_view kPolicyMetricsBridgeStage =
    "policy.metrics_bridge";

constexpr std::array<PolicyMetricKind, kPolicyMetricFamilyCount>
    kPolicyMetricKinds{
        PolicyMetricKind::ReloadTotal,
        PolicyMetricKind::InvalidTotal,
        PolicyMetricKind::PatchTotal,
        PolicyMetricKind::DenyTotal,
        PolicyMetricKind::RollbackTotal,
        PolicyMetricKind::ActiveGeneration,
        PolicyMetricKind::SafeModeTotal,
    };

std::string normalize_profile_id(std::string profile_id) {
  if (profile_id.empty()) {
    return std::string("unknown");
  }

  return profile_id;
}

std::string policy_metric_description(PolicyMetricKind kind) {
  switch (kind) {
    case PolicyMetricKind::ReloadTotal:
      return "policy successful and failed reload attempts";
    case PolicyMetricKind::InvalidTotal:
      return "policy invalid bundle and patch observations";
    case PolicyMetricKind::PatchTotal:
      return "policy patch apply attempts";
    case PolicyMetricKind::DenyTotal:
      return "policy deny decisions exposed to infra callers";
    case PolicyMetricKind::RollbackTotal:
      return "policy rollback attempts";
    case PolicyMetricKind::ActiveGeneration:
      return "policy active snapshot generation gauge";
    case PolicyMetricKind::SafeModeTotal:
      return "policy safe mode entries";
  }

  return "policy bridge metric";
}

metrics::MetricsOperationStatus make_metrics_failure_status(
    metrics::MetricsErrorCode error_code,
    std::string message) {
  const auto mapping = metrics::map_metrics_error_code(error_code);
  return metrics::MetricsOperationStatus::failure(
      mapping.result_code,
      std::move(message),
      std::string(kPolicyMetricsBridgeStage),
      std::string(kPolicyMetricsBridgeSourceRef));
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

std::string policy_error_code_label(const PolicyMetricSignal& signal) {
  if (!signal.policy_error_code.has_value()) {
    return std::string(kPolicyMetricNoErrorCodeLabel);
  }

  return std::string(policy_error_code_name(*signal.policy_error_code));
}

}  // namespace

metrics::MetricIdentity make_policy_metric_identity(PolicyMetricKind kind) {
  return metrics::MetricIdentity{
      .name = std::string(policy_metric_name(kind)),
      .type = policy_metric_type(kind),
      .unit = std::string(policy_metric_unit(kind)),
      .description = policy_metric_description(kind),
  };
}

PolicyMetricsBridge::PolicyMetricsBridge(
    std::shared_ptr<metrics::IMetricsProvider> metrics_provider,
    std::string profile_id)
    : metrics_provider_(std::move(metrics_provider)),
      profile_id_(normalize_profile_id(std::move(profile_id))) {}

PolicyMetricsEmitResult PolicyMetricsBridge::emit(
    const PolicyMetricSignal& signal) {
  ++emission_attempt_total_;

  if (!signal.has_consistent_values()) {
    return make_failure_result(
        metrics::MetricsErrorCode::ConfigInvalid,
        make_metrics_failure_status(
            metrics::MetricsErrorCode::ConfigInvalid,
            "policy metric signal violates the frozen stage/outcome/value rules"));
  }

  const auto sample = make_sample(signal);
  if (!sample.is_valid() ||
      !is_policy_metric_error_code(sample.labels.error_code)) {
    return make_failure_result(
        metrics::MetricsErrorCode::ConfigInvalid,
        make_metrics_failure_status(
            metrics::MetricsErrorCode::ConfigInvalid,
            "policy metric sample violates the frozen identity or label contract"));
  }

  PolicyMetricsEmitResult failure;
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
  return PolicyMetricsEmitResult{
      .emitted = true,
      .bridge_degraded = false,
      .status = status,
      .metrics_error_code = std::nullopt,
  };
}

bool PolicyMetricsBridge::ensure_meter_ready(
    PolicyMetricsEmitResult* failure) {
  if (meter_) {
    return true;
  }

  if (!metrics_provider_) {
    *failure = make_failure_result(
        metrics::MetricsErrorCode::ProviderNotReady,
        make_metrics_failure_status(
            metrics::MetricsErrorCode::ProviderNotReady,
            "policy metrics bridge requires a metrics provider before emitting samples"));
    return false;
  }

  auto meter = metrics_provider_->get_meter(metrics::MeterScope{
      .name = std::string(kPolicyMetricsMeterScopeName),
      .version = std::string(kPolicyMetricsMeterScopeVersion),
      .schema_url = std::string(),
  });
  if (!meter) {
    *failure = make_failure_result(
        metrics::MetricsErrorCode::ProviderNotReady,
        make_metrics_failure_status(
            metrics::MetricsErrorCode::ProviderNotReady,
            "metrics provider did not supply the infra.policy meter scope"));
    return false;
  }

  meter_ = std::move(meter);
  return true;
}

bool PolicyMetricsBridge::ensure_instruments_registered(
    PolicyMetricsEmitResult* failure) {
  if (instruments_registered_) {
    return true;
  }

  instrument_handles_.fill(std::nullopt);
  for (const auto kind : kPolicyMetricKinds) {
    const auto identity = make_policy_metric_identity(kind);
    std::optional<metrics::InstrumentHandle> handle;

    switch (policy_metric_type(kind)) {
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
              "policy metrics bridge could not register metric family " +
                  identity.name));
      return false;
    }

    instrument_handles_[to_index(kind)] = std::move(handle);
  }

  instruments_registered_ = true;
  return true;
}

PolicyMetricsEmitResult PolicyMetricsBridge::make_failure_result(
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

  return PolicyMetricsEmitResult{
      .emitted = false,
      .bridge_degraded = failure_causes_degraded,
      .status = std::move(status),
      .metrics_error_code = error_code,
  };
}

metrics::MetricSample PolicyMetricsBridge::make_sample(
    const PolicyMetricSignal& signal) const {
  return metrics::MetricSample{
      .identity_ref = make_policy_metric_identity(signal.kind),
      .value = signal.value,
      .ts_unix_ms = signal.ts_unix_ms,
      .labels = metrics::MetricLabels{
          .module = std::string(kPolicyMetricModuleLabel),
          .stage = signal.stage,
          .profile = profile_id_,
          .outcome = signal.outcome,
          .error_code = policy_error_code_label(signal),
      },
  };
}

}  // namespace dasall::infra::policy