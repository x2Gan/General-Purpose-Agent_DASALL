#include "bridges/ServiceMetricsBridge.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <utility>

#include "metrics/MetricTypes.h"

namespace dasall::services::internal {

struct ServiceMetricsBridge::ServiceMetricSignal {
  ServiceMetricKind kind = ServiceMetricKind::execution_requests_total;
  double value = 0.0;
  std::int64_t ts_unix_ms = 0;
  std::string stage;
  std::string outcome;
  std::string error_code;
};

namespace {

using infra::metrics::MetricIdentity;
using infra::metrics::MetricLabels;
using infra::metrics::MetricSample;
using infra::metrics::MetricType;
using infra::metrics::MetricsErrorCode;
using infra::metrics::MetricsOperationStatus;

constexpr std::string_view kServiceMetricsBridgeSourceRef = "ServiceMetricsBridge";
constexpr std::string_view kServiceMetricsBridgeStage = "services.metrics_bridge";
constexpr std::array<ServiceMetricKind, kServiceMetricFamilyCount> kServiceMetricKinds{
    ServiceMetricKind::execution_requests_total,
    ServiceMetricKind::execution_latency_ms,
    ServiceMetricKind::execution_circuit_open_total,
    ServiceMetricKind::data_query_requests_total,
    ServiceMetricKind::subscription_overflow_total,
    ServiceMetricKind::compensation_hint_total,
};

enum class ServiceMetricsGranularity {
  full = 0,
  partial,
  minimal,
};

[[nodiscard]] std::size_t to_index(ServiceMetricKind kind) {
  return static_cast<std::size_t>(kind);
}

[[nodiscard]] std::string normalize_profile_id(std::string profile_id) {
  if (profile_id.empty()) {
    return "unknown";
  }

  return profile_id;
}

[[nodiscard]] std::string normalize_granularity(std::string granularity) {
  if (granularity == "partial" || granularity == "minimal") {
    return granularity;
  }

  return "full";
}

[[nodiscard]] ServiceMetricsGranularity parse_granularity(
    const std::string_view& granularity) {
  if (granularity == "minimal") {
    return ServiceMetricsGranularity::minimal;
  }

  if (granularity == "partial") {
    return ServiceMetricsGranularity::partial;
  }

  return ServiceMetricsGranularity::full;
}

[[nodiscard]] std::string normalized_or(std::string value, std::string_view fallback) {
  if (value.empty()) {
    return std::string(fallback);
  }

  return value;
}

[[nodiscard]] std::string sanitize_stage_token(std::string_view value) {
  if (value.empty()) {
    return "unknown";
  }

  std::string token;
  token.reserve(value.size());
  for (const char ch : value) {
    const auto code = static_cast<unsigned char>(ch);
    if (std::isalnum(code) || ch == '.' || ch == '_' || ch == '-') {
      token.push_back(ch);
      continue;
    }

    token.push_back('_');
  }

  return token.empty() ? std::string("unknown") : token;
}

[[nodiscard]] std::string join_stage(std::initializer_list<std::string> parts) {
  std::string stage;
  bool first = true;
  for (const auto& part : parts) {
    if (part.empty()) {
      continue;
    }

    if (!first) {
      stage.push_back('.');
    }

    stage += part;
    first = false;
  }

  return stage.empty() ? std::string("services.unknown") : stage;
}

[[nodiscard]] std::string result_code_label(contracts::ResultCode result_code) {
  switch (result_code) {
    case contracts::ResultCode::ValidationFieldMissing:
      return "ValidationFieldMissing";
    case contracts::ResultCode::PolicyDenied:
      return "PolicyDenied";
    case contracts::ResultCode::ToolExecutionFailed:
      return "ToolExecutionFailed";
    case contracts::ResultCode::ProviderTimeout:
      return "ProviderTimeout";
    case contracts::ResultCode::RuntimeRetryExhausted:
      return "RuntimeRetryExhausted";
  }

  return "Unknown";
}

[[nodiscard]] std::string error_code_label_for(const std::optional<contracts::ErrorInfo>& error,
                                               contracts::ResultCode result_code) {
  if (!error.has_value()) {
    return std::string(kServiceMetricNoErrorCodeLabel);
  }

  return result_code_label(result_code);
}

[[nodiscard]] std::string outcome_for_command_result(const ExecutionCommandResult& result) {
  if (!result.error.has_value()) {
    return "success";
  }

  if (!result.side_effects.empty() || !result.compensation_hints.empty() ||
      result.error->retryable) {
    return "degraded";
  }

  return "failure";
}

[[nodiscard]] std::string outcome_for_query_result(const ExecutionQueryResult& result) {
  if (!result.error.has_value()) {
    return "success";
  }

  if (result.from_cache || result.error->retryable) {
    return "degraded";
  }

  return "failure";
}

[[nodiscard]] std::string outcome_for_data_result(const DataQueryResult& result) {
  if (!result.error.has_value()) {
    return "success";
  }

  if (result.from_cache || result.error->retryable) {
    return "degraded";
  }

  return "failure";
}

[[nodiscard]] std::string outcome_for_catalog_result(const DataCatalogResult& result) {
  if (!result.error.has_value()) {
    return "success";
  }

  return result.error->retryable ? "degraded" : "failure";
}

[[nodiscard]] std::string outcome_for_subscription_result(
    const ExecutionSubscriptionResult& result) {
  if (result.resync_required || result.dropped_count > 0U) {
    return "degraded";
  }

  if (!result.error.has_value()) {
    return "success";
  }

  return result.error->retryable ? "degraded" : "failure";
}

[[nodiscard]] MetricType metric_type(ServiceMetricKind kind) {
  switch (kind) {
    case ServiceMetricKind::execution_requests_total:
    case ServiceMetricKind::execution_circuit_open_total:
    case ServiceMetricKind::data_query_requests_total:
    case ServiceMetricKind::subscription_overflow_total:
    case ServiceMetricKind::compensation_hint_total:
      return MetricType::Counter;
    case ServiceMetricKind::execution_latency_ms:
      return MetricType::Histogram;
  }

  return MetricType::Counter;
}

[[nodiscard]] std::string metric_name(ServiceMetricKind kind) {
  switch (kind) {
    case ServiceMetricKind::execution_requests_total:
      return "services_execution_requests_total";
    case ServiceMetricKind::execution_latency_ms:
      return "services_execution_latency_ms";
    case ServiceMetricKind::execution_circuit_open_total:
      return "services_execution_circuit_open_total";
    case ServiceMetricKind::data_query_requests_total:
      return "services_data_query_requests_total";
    case ServiceMetricKind::subscription_overflow_total:
      return "services_subscription_overflow_total";
    case ServiceMetricKind::compensation_hint_total:
      return "services_compensation_hint_total";
  }

  return "services_metric_unknown";
}

[[nodiscard]] std::string metric_unit(ServiceMetricKind kind) {
  return kind == ServiceMetricKind::execution_latency_ms ? "ms" : "1";
}

[[nodiscard]] std::string metric_description(ServiceMetricKind kind) {
  switch (kind) {
    case ServiceMetricKind::execution_requests_total:
      return "services execution request total";
    case ServiceMetricKind::execution_latency_ms:
      return "services execution latency in milliseconds";
    case ServiceMetricKind::execution_circuit_open_total:
      return "services execution circuit-open total";
    case ServiceMetricKind::data_query_requests_total:
      return "services data query request total";
    case ServiceMetricKind::subscription_overflow_total:
      return "services subscription overflow total";
    case ServiceMetricKind::compensation_hint_total:
      return "services compensation hint total";
  }

  return "services metric";
}

[[nodiscard]] MetricIdentity make_metric_identity(ServiceMetricKind kind) {
  return MetricIdentity{
      .name = metric_name(kind),
      .type = metric_type(kind),
      .unit = metric_unit(kind),
      .description = metric_description(kind),
  };
}

[[nodiscard]] MetricsOperationStatus make_metrics_failure_status(
    MetricsErrorCode error_code,
    std::string message) {
  const auto mapping = infra::metrics::map_metrics_error_code(error_code);
  return MetricsOperationStatus::failure(mapping.result_code,
                                         std::move(message),
                                         std::string(kServiceMetricsBridgeStage),
                                         std::string(kServiceMetricsBridgeSourceRef));
}

[[nodiscard]] bool metrics_error_causes_degraded(MetricsErrorCode error_code) {
  switch (error_code) {
    case MetricsErrorCode::ProviderNotReady:
    case MetricsErrorCode::IdentityInvalid:
    case MetricsErrorCode::ExportFailure:
    case MetricsErrorCode::ExportTimeout:
    case MetricsErrorCode::ConfigInvalid:
      return true;
    case MetricsErrorCode::LabelCardinalityExceeded:
    case MetricsErrorCode::QueueFull:
      return false;
  }

  return true;
}

[[nodiscard]] MetricsErrorCode infer_metrics_error_code(
    const MetricsOperationStatus& status,
    MetricsErrorCode fallback) {
  switch (status.result_code) {
    case contracts::ResultCode::ProviderTimeout:
      return MetricsErrorCode::ExportFailure;
    case contracts::ResultCode::PolicyDenied:
      return MetricsErrorCode::LabelCardinalityExceeded;
    case contracts::ResultCode::ValidationFieldMissing:
      return MetricsErrorCode::ConfigInvalid;
    case contracts::ResultCode::RuntimeRetryExhausted:
      return MetricsErrorCode::QueueFull;
    default:
      return fallback;
  }
}

[[nodiscard]] ServiceMetricsEmitResult make_suppressed_result(std::string state_ref) {
  return ServiceMetricsEmitResult{
      .emitted = false,
      .bridge_degraded = false,
      .signal_suppressed = true,
      .status = MetricsOperationStatus::success(std::move(state_ref)),
      .metrics_error_code = std::nullopt,
  };
}

}  // namespace

ServiceMetricsBridge::ServiceMetricsBridge(
    std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider,
    ServiceMetricsBridgeOptions options)
    : metrics_provider_(std::move(metrics_provider)), options_(std::move(options)) {
  options_.profile_id = normalize_profile_id(std::move(options_.profile_id));
  options_.metrics_granularity =
      normalize_granularity(std::move(options_.metrics_granularity));
  options_.meter_scope_name = normalized_or(std::move(options_.meter_scope_name),
                                            kServiceMetricsMeterScopeName);
  options_.meter_scope_version = normalized_or(std::move(options_.meter_scope_version),
                                               kServiceMetricsMeterScopeVersion);
}

void ServiceMetricsBridge::set_metrics_provider(
    std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider,
    std::string profile_id) {
  metrics_provider_ = std::move(metrics_provider);
  if (!profile_id.empty()) {
    options_.profile_id = normalize_profile_id(std::move(profile_id));
  }

  meter_.reset();
  instrument_handles_.fill(std::nullopt);
  instruments_registered_ = false;
  degraded_ = false;
  last_metrics_error_code_.reset();
}

ServiceMetricsEmitResult ServiceMetricsBridge::record_execution_result(
    std::string_view action,
    std::string_view adapter_id,
    const ExecutionCommandResult& result,
    std::optional<std::uint64_t> latency_ms) {
  if (!is_enabled()) {
    return make_suppressed_result("metrics://services/disabled");
  }

  const auto action_token = sanitize_stage_token(action);
  const auto adapter_token = sanitize_stage_token(adapter_id);
  const auto outcome = outcome_for_command_result(result);
  const auto error_code = error_code_label_for(result.error, result.code);

  auto emission = emit_signal(ServiceMetricSignal{
      .kind = ServiceMetricKind::execution_requests_total,
      .value = 1.0,
      .ts_unix_ms = current_time_unix_ms(),
      .stage = join_stage({"execution", "command", action_token}),
      .outcome = outcome,
      .error_code = error_code,
  });
  if (!emission.status.ok) {
    return emission;
  }

  if (latency_ms.has_value()) {
    emission = emit_signal(ServiceMetricSignal{
        .kind = ServiceMetricKind::execution_latency_ms,
        .value = static_cast<double>(*latency_ms),
        .ts_unix_ms = current_time_unix_ms(),
        .stage = join_stage({"execution", "adapter", adapter_token, action_token}),
        .outcome = outcome,
        .error_code = error_code,
    });
    if (!emission.status.ok) {
      return emission;
    }
  }

  if (!result.compensation_hints.empty()) {
    emission = emit_signal(ServiceMetricSignal{
        .kind = ServiceMetricKind::compensation_hint_total,
        .value = static_cast<double>(result.compensation_hints.size()),
        .ts_unix_ms = current_time_unix_ms(),
        .stage = join_stage({"execution", "compensation", action_token}),
        .outcome = outcome,
        .error_code = error_code,
    });
  }

  return emission;
}

ServiceMetricsEmitResult ServiceMetricsBridge::record_execution_query_result(
    std::string_view query_kind,
    std::string_view adapter_id,
    const ExecutionQueryResult& result,
    std::optional<std::uint64_t> latency_ms) {
  if (!is_enabled()) {
    return make_suppressed_result("metrics://services/disabled");
  }

  const auto query_token = sanitize_stage_token(query_kind);
  const auto adapter_token = sanitize_stage_token(adapter_id);
  const auto cache_token = result.from_cache ? std::string("cache_hit")
                                             : std::string("cache_miss");
  const auto outcome = outcome_for_query_result(result);
  const auto error_code = error_code_label_for(result.error, result.code);

  auto emission = emit_signal(ServiceMetricSignal{
      .kind = ServiceMetricKind::execution_requests_total,
      .value = 1.0,
      .ts_unix_ms = current_time_unix_ms(),
      .stage = join_stage({"execution", "query", query_token, cache_token}),
      .outcome = outcome,
      .error_code = error_code,
  });
  if (!emission.status.ok) {
    return emission;
  }

  if (latency_ms.has_value()) {
    emission = emit_signal(ServiceMetricSignal{
        .kind = ServiceMetricKind::execution_latency_ms,
        .value = static_cast<double>(*latency_ms),
        .ts_unix_ms = current_time_unix_ms(),
        .stage = join_stage({"execution", "query_adapter", adapter_token, query_token}),
        .outcome = outcome,
        .error_code = error_code,
    });
  }

  return emission;
}

ServiceMetricsEmitResult ServiceMetricsBridge::record_execution_circuit_open(
    std::string_view action,
    std::string_view adapter_id,
    std::string_view reason) {
  if (!is_enabled()) {
    return make_suppressed_result("metrics://services/disabled");
  }

  return emit_signal(ServiceMetricSignal{
      .kind = ServiceMetricKind::execution_circuit_open_total,
      .value = 1.0,
      .ts_unix_ms = current_time_unix_ms(),
      .stage = join_stage({"execution",
                           "circuit",
                           sanitize_stage_token(action),
                           sanitize_stage_token(adapter_id)}),
      .outcome = "degraded",
      .error_code = sanitize_stage_token(reason),
  });
}

ServiceMetricsEmitResult ServiceMetricsBridge::record_data_query_result(
    std::string_view query_kind,
    const DataQueryResult& result,
    std::optional<std::uint64_t> latency_ms) {
  if (!is_enabled()) {
    return make_suppressed_result("metrics://services/disabled");
  }

  const auto query_token = sanitize_stage_token(query_kind);
  const auto cache_token = result.from_cache ? std::string("cache_hit")
                                             : std::string("cache_miss");
  const auto outcome = outcome_for_data_result(result);
  const auto error_code = error_code_label_for(result.error, result.code);

  auto emission = emit_signal(ServiceMetricSignal{
      .kind = ServiceMetricKind::data_query_requests_total,
      .value = 1.0,
      .ts_unix_ms = current_time_unix_ms(),
      .stage = join_stage({"data", "query", query_token, cache_token}),
      .outcome = outcome,
      .error_code = error_code,
  });
  if (!emission.status.ok) {
    return emission;
  }

  if (latency_ms.has_value()) {
    emission = emit_signal(ServiceMetricSignal{
        .kind = ServiceMetricKind::execution_latency_ms,
        .value = static_cast<double>(*latency_ms),
        .ts_unix_ms = current_time_unix_ms(),
        .stage = join_stage({"data", "query_latency", query_token}),
        .outcome = outcome,
        .error_code = error_code,
    });
  }

  return emission;
}

ServiceMetricsEmitResult ServiceMetricsBridge::record_data_catalog_result(
    std::string_view target_class,
    const DataCatalogResult& result,
    std::optional<std::uint64_t> latency_ms) {
  if (!is_enabled()) {
    return make_suppressed_result("metrics://services/disabled");
  }

  const auto class_token = sanitize_stage_token(target_class);
  const auto outcome = outcome_for_catalog_result(result);
  const auto error_code = error_code_label_for(result.error, result.code);

  auto emission = emit_signal(ServiceMetricSignal{
      .kind = ServiceMetricKind::data_query_requests_total,
      .value = 1.0,
      .ts_unix_ms = current_time_unix_ms(),
      .stage = join_stage({"data", "catalog", class_token}),
      .outcome = outcome,
      .error_code = error_code,
  });
  if (!emission.status.ok) {
    return emission;
  }

  if (latency_ms.has_value()) {
    emission = emit_signal(ServiceMetricSignal{
        .kind = ServiceMetricKind::execution_latency_ms,
        .value = static_cast<double>(*latency_ms),
        .ts_unix_ms = current_time_unix_ms(),
        .stage = join_stage({"data", "catalog_latency", class_token}),
        .outcome = outcome,
        .error_code = error_code,
    });
  }

  return emission;
}

ServiceMetricsEmitResult ServiceMetricsBridge::record_subscription_result(
    std::string_view capability_id,
    std::string_view stream_kind,
    const ExecutionSubscriptionResult& result) {
  if (!is_enabled()) {
    return make_suppressed_result("metrics://services/disabled");
  }

  if (!result.resync_required && result.dropped_count == 0U) {
    return make_suppressed_result("metrics://services/subscription/no_overflow");
  }

  return emit_signal(ServiceMetricSignal{
      .kind = ServiceMetricKind::subscription_overflow_total,
      .value = result.dropped_count == 0U ? 1.0
                                          : static_cast<double>(result.dropped_count),
      .ts_unix_ms = current_time_unix_ms(),
      .stage = join_stage({"subscription",
                           sanitize_stage_token(stream_kind),
                           sanitize_stage_token(capability_id)}),
      .outcome = outcome_for_subscription_result(result),
      .error_code = error_code_label_for(result.error, result.code),
  });
}

bool ServiceMetricsBridge::is_enabled() const {
  return options_.enabled;
}

bool ServiceMetricsBridge::granularity_allows(ServiceMetricKind kind) const {
  switch (parse_granularity(options_.metrics_granularity)) {
    case ServiceMetricsGranularity::full:
      return true;
    case ServiceMetricsGranularity::partial:
      return kind != ServiceMetricKind::execution_latency_ms;
    case ServiceMetricsGranularity::minimal:
      return kind == ServiceMetricKind::execution_requests_total ||
             kind == ServiceMetricKind::data_query_requests_total ||
             kind == ServiceMetricKind::subscription_overflow_total;
  }

  return true;
}

std::int64_t ServiceMetricsBridge::current_time_unix_ms() const {
  if (options_.now_ms) {
    return options_.now_ms();
  }

  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

ServiceMetricsEmitResult ServiceMetricsBridge::emit_signal(
    const ServiceMetricSignal& signal) {
  if (!granularity_allows(signal.kind)) {
    return make_suppressed_result("metrics://services/granularity_suppressed");
  }

  ++emission_attempt_total_;

  const auto sample = MetricSample{
      .identity_ref = make_metric_identity(signal.kind),
      .value = signal.value,
      .ts_unix_ms = signal.ts_unix_ms,
      .labels = MetricLabels{
          .module = "services",
          .stage = signal.stage,
          .profile = options_.profile_id,
          .outcome = signal.outcome,
          .error_code = signal.error_code,
      },
  };
  if (!sample.is_valid()) {
    return make_failure_result(
        MetricsErrorCode::ConfigInvalid,
        make_metrics_failure_status(MetricsErrorCode::ConfigInvalid,
                                    "service metric sample violates the frozen identity or label contract"));
  }

  ServiceMetricsEmitResult failure;
  if (!ensure_meter_ready(&failure)) {
    return failure;
  }

  if (!ensure_instruments_registered(&failure)) {
    return failure;
  }

  const auto status = meter_->record(sample);
  if (!status.ok) {
    return make_failure_result(
        infer_metrics_error_code(status, MetricsErrorCode::ExportFailure),
        status);
  }

  degraded_ = false;
  last_metrics_error_code_.reset();
  return ServiceMetricsEmitResult{
      .emitted = true,
      .bridge_degraded = false,
      .signal_suppressed = false,
      .status = status,
      .metrics_error_code = std::nullopt,
  };
}

bool ServiceMetricsBridge::ensure_meter_ready(ServiceMetricsEmitResult* failure) {
  if (meter_) {
    return true;
  }

  if (!metrics_provider_) {
    *failure = make_failure_result(
        MetricsErrorCode::ProviderNotReady,
        make_metrics_failure_status(MetricsErrorCode::ProviderNotReady,
                                    "service metrics bridge requires a metrics provider before emitting samples"));
    return false;
  }

  auto meter = metrics_provider_->get_meter(infra::metrics::MeterScope{
      .name = options_.meter_scope_name,
      .version = options_.meter_scope_version,
      .schema_url = std::string(),
  });
  if (!meter) {
    *failure = make_failure_result(
        MetricsErrorCode::ProviderNotReady,
        make_metrics_failure_status(MetricsErrorCode::ProviderNotReady,
                                    "metrics provider did not supply the services meter scope"));
    return false;
  }

  meter_ = std::move(meter);
  return true;
}

bool ServiceMetricsBridge::ensure_instruments_registered(
    ServiceMetricsEmitResult* failure) {
  if (instruments_registered_) {
    return true;
  }

  instrument_handles_.fill(std::nullopt);
  for (const auto kind : kServiceMetricKinds) {
    const auto identity = make_metric_identity(kind);
    std::optional<infra::metrics::InstrumentHandle> handle;

    switch (metric_type(kind)) {
      case MetricType::Counter:
        handle = meter_->create_counter(identity);
        break;
      case MetricType::Gauge:
        handle = meter_->create_gauge(identity);
        break;
      case MetricType::Histogram:
        handle = meter_->create_histogram(identity);
        break;
      case MetricType::UpDownCounter:
        handle = std::nullopt;
        break;
    }

    if (!handle.has_value() || !handle->is_valid()) {
      instrument_handles_.fill(std::nullopt);
      instruments_registered_ = false;
      meter_.reset();
      *failure = make_failure_result(
          MetricsErrorCode::IdentityInvalid,
          make_metrics_failure_status(MetricsErrorCode::IdentityInvalid,
                                      "service metrics bridge could not register metric family " +
                                          identity.name));
      return false;
    }

    instrument_handles_[to_index(kind)] = std::move(handle);
  }

  instruments_registered_ = true;
  return true;
}

ServiceMetricsEmitResult ServiceMetricsBridge::make_failure_result(
    MetricsErrorCode error_code,
    MetricsOperationStatus status) {
  ++emission_failure_total_;
  last_metrics_error_code_ = error_code;

  const bool failure_causes_degraded = metrics_error_causes_degraded(error_code);
  degraded_ = degraded_ || failure_causes_degraded;

  if (error_code == MetricsErrorCode::ProviderNotReady ||
      error_code == MetricsErrorCode::IdentityInvalid ||
      error_code == MetricsErrorCode::ConfigInvalid) {
    meter_.reset();
    instrument_handles_.fill(std::nullopt);
    instruments_registered_ = false;
  }

  return ServiceMetricsEmitResult{
      .emitted = false,
      .bridge_degraded = failure_causes_degraded,
      .signal_suppressed = false,
      .status = std::move(status),
      .metrics_error_code = error_code,
  };
}

}  // namespace dasall::services::internal