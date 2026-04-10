#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "logging/LoggingErrors.h"
#include "metrics/IMeter.h"
#include "metrics/IMetricsProvider.h"
#include "metrics/MetricsErrors.h"
#include "metrics/MetricTypes.h"

namespace dasall::infra::logging {

enum class LoggingMetricKind {
  WriteTotal = 0,
  WriteFailTotal,
  DropTotal,
  QueueDepth,
  FlushLatencyMs,
};

inline constexpr std::string_view kLoggingMetricsMeterScopeName = "infra.logging";
inline constexpr std::string_view kLoggingMetricsMeterScopeVersion = "v1";
inline constexpr std::string_view kLoggingMetricModuleLabel = "logging";
inline constexpr std::string_view kLoggingMetricNoErrorCodeLabel = "none";
inline constexpr std::array<std::string_view, 4> kLoggingMetricAllowedStages{
    "write",
    "queue",
    "flush",
    "recovery",
};
inline constexpr std::array<std::string_view, 3> kLoggingMetricAllowedOutcomes{
    "success",
    "failure",
    "degraded",
};
inline constexpr std::size_t kLoggingMetricFamilyCount = 5U;

[[nodiscard]] inline constexpr std::string_view logging_metric_name(
    LoggingMetricKind kind) {
  switch (kind) {
    case LoggingMetricKind::WriteTotal:
      return "logging_write_total";
    case LoggingMetricKind::WriteFailTotal:
      return "logging_write_fail_total";
    case LoggingMetricKind::DropTotal:
      return "logging_drop_total";
    case LoggingMetricKind::QueueDepth:
      return "logging_queue_depth";
    case LoggingMetricKind::FlushLatencyMs:
      return "logging_flush_latency_ms";
  }

  return "logging_unknown_metric";
}

[[nodiscard]] inline constexpr metrics::MetricType logging_metric_type(
    LoggingMetricKind kind) {
  switch (kind) {
    case LoggingMetricKind::WriteTotal:
    case LoggingMetricKind::WriteFailTotal:
    case LoggingMetricKind::DropTotal:
      return metrics::MetricType::Counter;
    case LoggingMetricKind::QueueDepth:
      return metrics::MetricType::Gauge;
    case LoggingMetricKind::FlushLatencyMs:
      return metrics::MetricType::Histogram;
  }

  return metrics::MetricType::Counter;
}

[[nodiscard]] inline constexpr std::string_view logging_metric_unit(
    LoggingMetricKind kind) {
  switch (kind) {
    case LoggingMetricKind::FlushLatencyMs:
      return "ms";
    case LoggingMetricKind::WriteTotal:
    case LoggingMetricKind::WriteFailTotal:
    case LoggingMetricKind::DropTotal:
    case LoggingMetricKind::QueueDepth:
      return "1";
  }

  return "1";
}

[[nodiscard]] inline bool is_logging_metric_stage(std::string_view stage) {
  return std::find(kLoggingMetricAllowedStages.begin(),
                   kLoggingMetricAllowedStages.end(),
                   stage) != kLoggingMetricAllowedStages.end();
}

[[nodiscard]] inline bool is_logging_metric_outcome(std::string_view outcome) {
  return std::find(kLoggingMetricAllowedOutcomes.begin(),
                   kLoggingMetricAllowedOutcomes.end(),
                   outcome) != kLoggingMetricAllowedOutcomes.end();
}

[[nodiscard]] inline bool is_logging_metric_error_code(
  const std::string_view& error_code) {
  return error_code == kLoggingMetricNoErrorCodeLabel ||
         error_code == logging_error_code_name(LoggingErrorCode::QueueFull) ||
         error_code == logging_error_code_name(LoggingErrorCode::SinkIo) ||
         error_code == logging_error_code_name(LoggingErrorCode::FormatInvalid) ||
         error_code == logging_error_code_name(LoggingErrorCode::ConfigInvalid);
}

[[nodiscard]] metrics::MetricIdentity make_logging_metric_identity(
    LoggingMetricKind kind);

struct LoggingMetricSignal {
  LoggingMetricKind kind = LoggingMetricKind::WriteTotal;
  double value = 0.0;
  std::int64_t ts_unix_ms = 0;
  std::string stage = std::string(kLoggingMetricAllowedStages.front());
  std::string outcome = std::string(kLoggingMetricAllowedOutcomes.front());
  std::optional<LoggingErrorCode> logging_error_code;

  [[nodiscard]] bool has_consistent_values() const {
    if (!std::isfinite(value) || value < 0.0 || ts_unix_ms <= 0 ||
        !is_logging_metric_stage(stage) ||
        !is_logging_metric_outcome(outcome)) {
      return false;
    }

    if (outcome == "success" && logging_error_code.has_value()) {
      return false;
    }

    switch (kind) {
      case LoggingMetricKind::WriteTotal:
        return outcome != "failure";
      case LoggingMetricKind::WriteFailTotal:
        return outcome == "failure" && logging_error_code.has_value();
      case LoggingMetricKind::DropTotal:
        return outcome != "success" && logging_error_code.has_value();
      case LoggingMetricKind::QueueDepth:
      case LoggingMetricKind::FlushLatencyMs:
        return true;
    }

    return false;
  }
};

struct LoggingMetricsEmitResult {
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

class LoggingMetricsBridge {
 public:
  explicit LoggingMetricsBridge(
      std::shared_ptr<metrics::IMetricsProvider> metrics_provider,
      std::string profile_id = "unknown");

  LoggingMetricsEmitResult emit(const LoggingMetricSignal& signal);

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
  static constexpr std::size_t to_index(LoggingMetricKind kind) {
    return static_cast<std::size_t>(kind);
  }

  bool ensure_meter_ready(LoggingMetricsEmitResult* failure);
  bool ensure_instruments_registered(LoggingMetricsEmitResult* failure);
  LoggingMetricsEmitResult make_failure_result(
      metrics::MetricsErrorCode error_code,
      metrics::MetricsOperationStatus status);
  metrics::MetricSample make_sample(const LoggingMetricSignal& signal) const;

  std::shared_ptr<metrics::IMetricsProvider> metrics_provider_;
  std::shared_ptr<metrics::IMeter> meter_;
  std::string profile_id_;
  bool degraded_ = false;
  bool instruments_registered_ = false;
  std::array<std::optional<metrics::InstrumentHandle>,
             kLoggingMetricFamilyCount>
      instrument_handles_{};
  std::uint64_t emission_attempt_total_ = 0;
  std::uint64_t emission_failure_total_ = 0;
  std::optional<metrics::MetricsErrorCode> last_metrics_error_code_;
};

}  // namespace dasall::infra::logging