#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "metrics/IMeter.h"
#include "metrics/IMetricsProvider.h"
#include "metrics/MetricsErrors.h"
#include "metrics/MetricTypes.h"
#include "tracing/TraceErrors.h"

namespace dasall::infra::tracing {

enum class TraceMetricKind {
  SpanStartedTotal = 0,
  SpanEndedTotal,
  SpanDroppedTotal,
  ExportSuccessTotal,
  ExportFailureTotal,
  ExportLatencyMs,
  BatchQueueDepth,
  ContextInvalidTotal,
};

inline constexpr std::string_view kTraceMetricsMeterScopeName = "infra.tracing";
inline constexpr std::string_view kTraceMetricsMeterScopeVersion = "v1";
inline constexpr std::string_view kTraceMetricModuleLabel = "tracing";
inline constexpr std::string_view kTraceMetricNoErrorCodeLabel = "none";
inline constexpr std::array<std::string_view, 4> kTraceMetricAllowedStages{
    "span",
    "queue",
    "export",
    "context",
};
inline constexpr std::array<std::string_view, 3> kTraceMetricAllowedOutcomes{
    "success",
    "failure",
    "degraded",
};
inline constexpr std::size_t kTraceMetricFamilyCount = 8U;

[[nodiscard]] inline constexpr std::string_view trace_metric_name(
    TraceMetricKind kind) {
  switch (kind) {
    case TraceMetricKind::SpanStartedTotal:
      return "trace_span_started_total";
    case TraceMetricKind::SpanEndedTotal:
      return "trace_span_ended_total";
    case TraceMetricKind::SpanDroppedTotal:
      return "trace_span_dropped_total";
    case TraceMetricKind::ExportSuccessTotal:
      return "trace_export_success_total";
    case TraceMetricKind::ExportFailureTotal:
      return "trace_export_failure_total";
    case TraceMetricKind::ExportLatencyMs:
      return "trace_export_latency_ms";
    case TraceMetricKind::BatchQueueDepth:
      return "trace_batch_queue_depth";
    case TraceMetricKind::ContextInvalidTotal:
      return "trace_context_invalid_total";
  }

  return "trace_unknown_metric";
}

[[nodiscard]] inline constexpr metrics::MetricType trace_metric_type(
    TraceMetricKind kind) {
  switch (kind) {
    case TraceMetricKind::ExportLatencyMs:
      return metrics::MetricType::Histogram;
    case TraceMetricKind::BatchQueueDepth:
      return metrics::MetricType::Gauge;
    case TraceMetricKind::SpanStartedTotal:
    case TraceMetricKind::SpanEndedTotal:
    case TraceMetricKind::SpanDroppedTotal:
    case TraceMetricKind::ExportSuccessTotal:
    case TraceMetricKind::ExportFailureTotal:
    case TraceMetricKind::ContextInvalidTotal:
      return metrics::MetricType::Counter;
  }

  return metrics::MetricType::Counter;
}

[[nodiscard]] inline constexpr std::string_view trace_metric_unit(
    TraceMetricKind kind) {
  switch (kind) {
    case TraceMetricKind::ExportLatencyMs:
      return "ms";
    case TraceMetricKind::SpanStartedTotal:
    case TraceMetricKind::SpanEndedTotal:
    case TraceMetricKind::SpanDroppedTotal:
    case TraceMetricKind::ExportSuccessTotal:
    case TraceMetricKind::ExportFailureTotal:
    case TraceMetricKind::BatchQueueDepth:
    case TraceMetricKind::ContextInvalidTotal:
      return "1";
  }

  return "1";
}

[[nodiscard]] inline bool is_trace_metric_stage(
  const std::string_view& stage) {
  return std::find(kTraceMetricAllowedStages.begin(),
                   kTraceMetricAllowedStages.end(),
                   stage) != kTraceMetricAllowedStages.end();
}

[[nodiscard]] inline bool is_trace_metric_outcome(
  const std::string_view& outcome) {
  return std::find(kTraceMetricAllowedOutcomes.begin(),
                   kTraceMetricAllowedOutcomes.end(),
                   outcome) != kTraceMetricAllowedOutcomes.end();
}

[[nodiscard]] inline bool is_trace_metric_error_code(
  const std::string_view& error_code) {
  return error_code == kTraceMetricNoErrorCodeLabel ||
         error_code == trace_error_code_name(TraceErrorCode::ProviderNotReady) ||
         error_code == trace_error_code_name(TraceErrorCode::InvalidContext) ||
         error_code == trace_error_code_name(TraceErrorCode::QueueFull) ||
         error_code == trace_error_code_name(TraceErrorCode::ExportTimeout) ||
         error_code == trace_error_code_name(TraceErrorCode::ExportFailure) ||
         error_code == trace_error_code_name(TraceErrorCode::ShutdownTimeout) ||
         error_code == trace_error_code_name(TraceErrorCode::ConfigInvalid);
}

[[nodiscard]] metrics::MetricIdentity make_trace_metric_identity(
    TraceMetricKind kind);

struct TraceMetricSignal {
  TraceMetricKind kind = TraceMetricKind::SpanStartedTotal;
  double value = 0.0;
  std::int64_t ts_unix_ms = 0;
  std::string stage = std::string(kTraceMetricAllowedStages.front());
  std::string outcome = std::string(kTraceMetricAllowedOutcomes.front());
  std::optional<TraceErrorCode> trace_error_code;

  [[nodiscard]] bool has_consistent_values() const {
    if (!std::isfinite(value) || value < 0.0 || ts_unix_ms <= 0 ||
        !is_trace_metric_stage(stage) || !is_trace_metric_outcome(outcome)) {
      return false;
    }

    if (outcome == "success" && trace_error_code.has_value()) {
      return false;
    }

    switch (kind) {
      case TraceMetricKind::SpanStartedTotal:
      case TraceMetricKind::SpanEndedTotal:
        return stage == "span" && outcome == "success" &&
               !trace_error_code.has_value();
      case TraceMetricKind::SpanDroppedTotal:
        return (stage == "span" || stage == "queue") && outcome != "success" &&
               trace_error_code.has_value();
      case TraceMetricKind::ExportSuccessTotal:
        return stage == "export" && outcome == "success" &&
               !trace_error_code.has_value();
      case TraceMetricKind::ExportFailureTotal:
        return stage == "export" && outcome != "success" &&
               trace_error_code.has_value();
      case TraceMetricKind::ExportLatencyMs:
        return stage == "export";
      case TraceMetricKind::BatchQueueDepth:
        return stage == "queue" && !trace_error_code.has_value();
      case TraceMetricKind::ContextInvalidTotal:
        return stage == "context" && outcome == "failure" &&
               trace_error_code == TraceErrorCode::InvalidContext;
    }

    return false;
  }
};

struct TraceMetricsEmitResult {
  bool emitted = false;
  bool bridge_degraded = false;
  metrics::MetricsOperationStatus status =
      metrics::MetricsOperationStatus::success();
  std::optional<metrics::MetricsErrorCode> metrics_error_code;

  [[nodiscard]] bool has_consistent_state() const {
    if (emitted) {
      return status.ok && !metrics_error_code.has_value();
    }

    return !status.ok && metrics_error_code.has_value();
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    return status.references_only_contract_error_types();
  }
};

class TraceMetricsBridge {
 public:
  explicit TraceMetricsBridge(
      std::shared_ptr<metrics::IMetricsProvider> metrics_provider,
      std::string profile_id = "unknown");

  TraceMetricsEmitResult emit(const TraceMetricSignal& signal);

  [[nodiscard]] bool is_degraded() const {
    return degraded_;
  }

  [[nodiscard]] bool has_active_meter() const {
    return static_cast<bool>(meter_);
  }

  [[nodiscard]] bool instruments_registered() const {
    return instruments_registered_;
  }

  [[nodiscard]] const std::string& profile_id() const {
    return profile_id_;
  }

  [[nodiscard]] std::uint64_t emission_attempt_total() const {
    return emission_attempt_total_;
  }

  [[nodiscard]] std::uint64_t emission_failure_total() const {
    return emission_failure_total_;
  }

  [[nodiscard]] std::optional<metrics::MetricsErrorCode> last_metrics_error_code()
      const {
    return last_metrics_error_code_;
  }

 private:
  static constexpr std::size_t to_index(TraceMetricKind kind) {
    return static_cast<std::size_t>(kind);
  }

  bool ensure_meter_ready(TraceMetricsEmitResult* failure);
  bool ensure_instruments_registered(TraceMetricsEmitResult* failure);
  TraceMetricsEmitResult make_failure_result(
      metrics::MetricsErrorCode error_code,
      metrics::MetricsOperationStatus status);
  metrics::MetricSample make_sample(const TraceMetricSignal& signal) const;

  std::shared_ptr<metrics::IMetricsProvider> metrics_provider_;
  std::shared_ptr<metrics::IMeter> meter_;
  std::string profile_id_;
  bool degraded_ = false;
  bool instruments_registered_ = false;
  std::array<std::optional<metrics::InstrumentHandle>,
             kTraceMetricFamilyCount>
      instrument_handles_{};
  std::uint64_t emission_attempt_total_ = 0;
  std::uint64_t emission_failure_total_ = 0;
  std::optional<metrics::MetricsErrorCode> last_metrics_error_code_;
};

}  // namespace dasall::infra::tracing