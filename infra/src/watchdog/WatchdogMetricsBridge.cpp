#include "watchdog/WatchdogMetricsBridge.h"

#include <array>
#include <string>
#include <utility>

namespace dasall::infra::watchdog {
namespace {

constexpr std::string_view kWatchdogMetricsBridgeSourceRef =
    "WatchdogMetricsBridge";
constexpr std::string_view kWatchdogMetricsBridgeStage =
    "watchdog.metrics_bridge";

constexpr std::array<WatchdogMetricKind, kWatchdogMetricFamilyCount>
    kWatchdogMetricKinds{
        WatchdogMetricKind::EntitiesTotal,
        WatchdogMetricKind::HeartbeatIngestTotal,
        WatchdogMetricKind::TimeoutTotal,
        WatchdogMetricKind::ConsecutiveMiss,
        WatchdogMetricKind::ScanLagMs,
        WatchdogMetricKind::EventPublishFailTotal,
        WatchdogMetricKind::SafeModeTotal,
    };

[[nodiscard]] std::string normalize_profile_id(std::string profile_id) {
  if (profile_id.empty()) {
    return std::string("unknown");
  }

  return profile_id;
}

[[nodiscard]] std::string watchdog_metric_description(
    WatchdogMetricKind kind) {
  switch (kind) {
    case WatchdogMetricKind::EntitiesTotal:
      return "watchdog registered entity total snapshot";
    case WatchdogMetricKind::HeartbeatIngestTotal:
      return "watchdog accepted heartbeat ingest totals by entity type";
    case WatchdogMetricKind::TimeoutTotal:
      return "watchdog timeout totals by entity type and timeout level";
    case WatchdogMetricKind::ConsecutiveMiss:
      return "watchdog consecutive miss gauge by entity id";
    case WatchdogMetricKind::ScanLagMs:
      return "watchdog scan lag gauge in milliseconds";
    case WatchdogMetricKind::EventPublishFailTotal:
      return "watchdog timeout event publish failures";
    case WatchdogMetricKind::SafeModeTotal:
      return "watchdog safe mode entries";
  }

  return "watchdog bridge metric";
}

[[nodiscard]] metrics::MetricsOperationStatus make_metrics_failure_status(
    metrics::MetricsErrorCode error_code,
    std::string message) {
  const auto mapping = metrics::map_metrics_error_code(error_code);
  return metrics::MetricsOperationStatus::failure(
      mapping.result_code,
      std::move(message),
      std::string(kWatchdogMetricsBridgeStage),
      std::string(kWatchdogMetricsBridgeSourceRef));
}

[[nodiscard]] bool metrics_error_causes_degraded(
    metrics::MetricsErrorCode error_code) {
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

[[nodiscard]] metrics::MetricsErrorCode infer_metrics_error_code(
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

[[nodiscard]] std::string watchdog_metric_stage(
    const WatchdogMetricSignal& signal) {
  switch (signal.kind) {
    case WatchdogMetricKind::EntitiesTotal:
      return "entities_total";
    case WatchdogMetricKind::HeartbeatIngestTotal:
      return "heartbeat_ingest/" + signal.entity_type;
    case WatchdogMetricKind::TimeoutTotal:
      return "timeout/" + signal.entity_type;
    case WatchdogMetricKind::ConsecutiveMiss:
      return "consecutive_miss/" + signal.entity_id;
    case WatchdogMetricKind::ScanLagMs:
      return "scan_lag";
    case WatchdogMetricKind::EventPublishFailTotal:
      return "event_publish_fail";
    case WatchdogMetricKind::SafeModeTotal:
      return "safe_mode";
  }

  return "watchdog_unknown";
}

[[nodiscard]] std::string watchdog_metric_outcome(
    const WatchdogMetricSignal& signal) {
  switch (signal.kind) {
    case WatchdogMetricKind::EntitiesTotal:
      return "snapshot";
    case WatchdogMetricKind::HeartbeatIngestTotal:
      return "accepted";
    case WatchdogMetricKind::TimeoutTotal:
      return std::string(watchdog_timeout_level_name(signal.timeout_level));
    case WatchdogMetricKind::ConsecutiveMiss:
      return signal.value > 0.0 ? "degraded" : "snapshot";
    case WatchdogMetricKind::ScanLagMs:
      return signal.watchdog_error_code.has_value() ? "degraded" : "snapshot";
    case WatchdogMetricKind::EventPublishFailTotal:
      return "failure";
    case WatchdogMetricKind::SafeModeTotal:
      return "degraded";
  }

  return "failure";
}

[[nodiscard]] std::string watchdog_metric_error_code_label(
    const WatchdogMetricSignal& signal) {
  if (!signal.watchdog_error_code.has_value()) {
    return std::string(kWatchdogMetricNoErrorCodeLabel);
  }

  return std::string(watchdog_error_code_name(*signal.watchdog_error_code));
}

}  // namespace

metrics::MetricIdentity make_watchdog_metric_identity(WatchdogMetricKind kind) {
  return metrics::MetricIdentity{
      .name = std::string(watchdog_metric_name(kind)),
      .type = watchdog_metric_type(kind),
      .unit = std::string(watchdog_metric_unit(kind)),
      .description = watchdog_metric_description(kind),
  };
}

WatchdogMetricsBridge::WatchdogMetricsBridge(
    std::shared_ptr<metrics::IMetricsProvider> metrics_provider,
    std::string profile_id)
    : metrics_provider_(std::move(metrics_provider)),
      profile_id_(normalize_profile_id(std::move(profile_id))) {}

WatchdogMetricsEmitResult WatchdogMetricsBridge::record_entities_total(
    std::uint64_t total_entities,
    std::int64_t ts_unix_ms) {
  WatchdogMetricSignal signal{};
  signal.kind = WatchdogMetricKind::EntitiesTotal;
  signal.value = static_cast<double>(total_entities);
  signal.ts_unix_ms = ts_unix_ms;
  return emit(signal);
}

WatchdogMetricsEmitResult WatchdogMetricsBridge::record_heartbeat_ingest(
    std::string entity_type,
    std::int64_t ts_unix_ms,
    double count) {
  WatchdogMetricSignal signal{};
  signal.kind = WatchdogMetricKind::HeartbeatIngestTotal;
  signal.value = count;
  signal.ts_unix_ms = ts_unix_ms;
  signal.entity_type = std::move(entity_type);
  return emit(signal);
}

WatchdogMetricsEmitResult WatchdogMetricsBridge::record_timeout(
    const TimeoutDecision& decision,
    std::string entity_type,
    std::int64_t ts_unix_ms,
    double count) {
  if (!decision.has_required_fields()) {
    return make_failure_result(
        metrics::MetricsErrorCode::ConfigInvalid,
        make_metrics_failure_status(
            metrics::MetricsErrorCode::ConfigInvalid,
            "watchdog metrics bridge requires a valid TimeoutDecision before timeout metrics can be emitted"));
  }

  WatchdogMetricSignal signal{};
  signal.kind = WatchdogMetricKind::TimeoutTotal;
  signal.value = count;
  signal.ts_unix_ms = ts_unix_ms;
  signal.entity_type = std::move(entity_type);
  signal.timeout_level = decision.timeout_level;
  return emit(signal);
}

WatchdogMetricsEmitResult WatchdogMetricsBridge::record_consecutive_miss(
    std::string entity_id,
    std::uint32_t consecutive_miss,
    std::int64_t ts_unix_ms) {
  WatchdogMetricSignal signal{};
  signal.kind = WatchdogMetricKind::ConsecutiveMiss;
  signal.value = static_cast<double>(consecutive_miss);
  signal.ts_unix_ms = ts_unix_ms;
  signal.entity_id = std::move(entity_id);
  return emit(signal);
}

WatchdogMetricsEmitResult WatchdogMetricsBridge::record_scan_lag(
    std::int64_t scan_lag_ms,
    std::int64_t ts_unix_ms,
    bool overdue) {
  WatchdogMetricSignal signal{};
  signal.kind = WatchdogMetricKind::ScanLagMs;
  signal.value = static_cast<double>(scan_lag_ms < 0 ? 0 : scan_lag_ms);
  signal.ts_unix_ms = ts_unix_ms;
  signal.watchdog_error_code = overdue
                                   ? std::optional<WatchdogErrorCode>(
                                         WatchdogErrorCode::ScanOverdue)
                                   : std::nullopt;
  return emit(signal);
}

WatchdogMetricsEmitResult WatchdogMetricsBridge::record_publish_fail(
    std::int64_t ts_unix_ms,
    double count) {
  WatchdogMetricSignal signal{};
  signal.kind = WatchdogMetricKind::EventPublishFailTotal;
  signal.value = count;
  signal.ts_unix_ms = ts_unix_ms;
  signal.watchdog_error_code = WatchdogErrorCode::EventPublishFail;
  return emit(signal);
}

WatchdogMetricsEmitResult WatchdogMetricsBridge::record_safe_mode(
    std::int64_t ts_unix_ms,
    double count) {
  WatchdogMetricSignal signal{};
  signal.kind = WatchdogMetricKind::SafeModeTotal;
  signal.value = count;
  signal.ts_unix_ms = ts_unix_ms;
  return emit(signal);
}

WatchdogMetricsEmitResult WatchdogMetricsBridge::emit(
    const WatchdogMetricSignal& signal) {
  ++emission_attempt_total_;

  if (!signal.has_consistent_values()) {
    return make_failure_result(
        metrics::MetricsErrorCode::ConfigInvalid,
        make_metrics_failure_status(
            metrics::MetricsErrorCode::ConfigInvalid,
            "watchdog metric signal violates the frozen kind/value/label rules"));
  }

  const auto sample = make_sample(signal);
  if (!sample.is_valid() ||
      !is_watchdog_metric_stage(sample.labels.stage) ||
      !is_watchdog_metric_outcome(sample.labels.outcome) ||
      !is_watchdog_metric_error_code(sample.labels.error_code)) {
    return make_failure_result(
        metrics::MetricsErrorCode::ConfigInvalid,
        make_metrics_failure_status(
            metrics::MetricsErrorCode::ConfigInvalid,
            "watchdog metric sample violates the frozen identity or label contract"));
  }

  WatchdogMetricsEmitResult failure;
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
  return WatchdogMetricsEmitResult{
      .emitted = true,
      .bridge_degraded = false,
      .status = status,
      .metrics_error_code = std::nullopt,
  };
}

bool WatchdogMetricsBridge::ensure_meter_ready(
    WatchdogMetricsEmitResult* failure) {
  if (meter_) {
    return true;
  }

  if (!metrics_provider_) {
    *failure = make_failure_result(
        metrics::MetricsErrorCode::ProviderNotReady,
        make_metrics_failure_status(
            metrics::MetricsErrorCode::ProviderNotReady,
            "watchdog metrics bridge requires a metrics provider before emitting samples"));
    return false;
  }

  auto meter = metrics_provider_->get_meter(metrics::MeterScope{
      .name = std::string(kWatchdogMetricsMeterScopeName),
      .version = std::string(kWatchdogMetricsMeterScopeVersion),
      .schema_url = std::string(),
  });
  if (!meter) {
    *failure = make_failure_result(
        metrics::MetricsErrorCode::ProviderNotReady,
        make_metrics_failure_status(
            metrics::MetricsErrorCode::ProviderNotReady,
            "metrics provider did not supply the infra.watchdog meter scope"));
    return false;
  }

  meter_ = std::move(meter);
  return true;
}

bool WatchdogMetricsBridge::ensure_instruments_registered(
    WatchdogMetricsEmitResult* failure) {
  if (instruments_registered_) {
    return true;
  }

  instrument_handles_.fill(std::nullopt);
  for (const auto kind : kWatchdogMetricKinds) {
    const auto identity = make_watchdog_metric_identity(kind);
    std::optional<metrics::InstrumentHandle> handle;

    switch (watchdog_metric_type(kind)) {
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
              "watchdog metrics bridge could not register metric family " +
                  identity.name));
      return false;
    }

    instrument_handles_[to_index(kind)] = std::move(handle);
  }

  instruments_registered_ = true;
  return true;
}

WatchdogMetricsEmitResult WatchdogMetricsBridge::make_failure_result(
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

  return WatchdogMetricsEmitResult{
      .emitted = false,
      .bridge_degraded = failure_causes_degraded,
      .status = std::move(status),
      .metrics_error_code = error_code,
  };
}

metrics::MetricSample WatchdogMetricsBridge::make_sample(
    const WatchdogMetricSignal& signal) const {
  return metrics::MetricSample{
      .identity_ref = make_watchdog_metric_identity(signal.kind),
      .value = signal.value,
      .ts_unix_ms = signal.ts_unix_ms,
      .labels = metrics::MetricLabels{
          .module = std::string(kWatchdogMetricModuleLabel),
          .stage = watchdog_metric_stage(signal),
          .profile = profile_id_,
          .outcome = watchdog_metric_outcome(signal),
          .error_code = watchdog_metric_error_code_label(signal),
      },
  };
}

}  // namespace dasall::infra::watchdog