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
#include "watchdog/TimeoutDecision.h"
#include "watchdog/WatchdogErrors.h"

namespace dasall::infra::watchdog {

enum class WatchdogMetricKind {
  EntitiesTotal = 0,
  HeartbeatIngestTotal,
  TimeoutTotal,
  ConsecutiveMiss,
  ScanLagMs,
  EventPublishFailTotal,
  SafeModeTotal,
};

inline constexpr std::string_view kWatchdogMetricsMeterScopeName =
    "infra.watchdog";
inline constexpr std::string_view kWatchdogMetricsMeterScopeVersion = "v1";
inline constexpr std::string_view kWatchdogMetricModuleLabel = "watchdog";
inline constexpr std::string_view kWatchdogMetricNoErrorCodeLabel = "none";
inline constexpr std::array<std::string_view, 7> kWatchdogMetricAllowedOutcomes{
    "snapshot",
    "accepted",
    "warning",
    "critical",
    "fatal",
    "degraded",
    "failure",
};
inline constexpr std::size_t kWatchdogMetricFamilyCount = 7U;

[[nodiscard]] inline constexpr std::string_view watchdog_metric_name(
    WatchdogMetricKind kind) {
  switch (kind) {
    case WatchdogMetricKind::EntitiesTotal:
      return "infra_watchdog_entities_total";
    case WatchdogMetricKind::HeartbeatIngestTotal:
      return "infra_watchdog_heartbeat_ingest_total";
    case WatchdogMetricKind::TimeoutTotal:
      return "infra_watchdog_timeout_total";
    case WatchdogMetricKind::ConsecutiveMiss:
      return "infra_watchdog_consecutive_miss";
    case WatchdogMetricKind::ScanLagMs:
      return "infra_watchdog_scan_lag_ms";
    case WatchdogMetricKind::EventPublishFailTotal:
      return "infra_watchdog_event_publish_fail_total";
    case WatchdogMetricKind::SafeModeTotal:
      return "infra_watchdog_safe_mode_total";
  }

  return "infra_watchdog_unknown_metric";
}

[[nodiscard]] inline constexpr metrics::MetricType watchdog_metric_type(
    WatchdogMetricKind kind) {
  switch (kind) {
    case WatchdogMetricKind::EntitiesTotal:
    case WatchdogMetricKind::ConsecutiveMiss:
    case WatchdogMetricKind::ScanLagMs:
      return metrics::MetricType::Gauge;
    case WatchdogMetricKind::HeartbeatIngestTotal:
    case WatchdogMetricKind::TimeoutTotal:
    case WatchdogMetricKind::EventPublishFailTotal:
    case WatchdogMetricKind::SafeModeTotal:
      return metrics::MetricType::Counter;
  }

  return metrics::MetricType::Counter;
}

[[nodiscard]] inline constexpr std::string_view watchdog_metric_unit(
    WatchdogMetricKind kind) {
  switch (kind) {
    case WatchdogMetricKind::ScanLagMs:
      return "ms";
    case WatchdogMetricKind::EntitiesTotal:
    case WatchdogMetricKind::HeartbeatIngestTotal:
    case WatchdogMetricKind::TimeoutTotal:
    case WatchdogMetricKind::ConsecutiveMiss:
    case WatchdogMetricKind::EventPublishFailTotal:
    case WatchdogMetricKind::SafeModeTotal:
      return "1";
  }

  return "1";
}

[[nodiscard]] inline bool is_watchdog_metric_token(std::string_view token) {
  return !token.empty() && metrics::is_valid_metric_name(token);
}

[[nodiscard]] inline bool is_watchdog_metric_outcome(std::string_view outcome) {
  return std::find(kWatchdogMetricAllowedOutcomes.begin(),
                   kWatchdogMetricAllowedOutcomes.end(),
                   outcome) != kWatchdogMetricAllowedOutcomes.end();
}

[[nodiscard]] inline bool is_watchdog_metric_error_code(
    std::string_view error_code) {
  return error_code == kWatchdogMetricNoErrorCodeLabel ||
         error_code == watchdog_error_code_name(WatchdogErrorCode::ScanOverdue) ||
         error_code ==
             watchdog_error_code_name(WatchdogErrorCode::EventPublishFail);
}

[[nodiscard]] inline bool is_watchdog_metric_stage(std::string_view stage) {
  if (stage == "entities_total" || stage == "scan_lag" ||
      stage == "event_publish_fail" || stage == "safe_mode") {
    return true;
  }

  constexpr std::string_view kHeartbeatPrefix = "heartbeat_ingest/";
  if (stage.rfind(kHeartbeatPrefix, 0) == 0) {
    return is_watchdog_metric_token(stage.substr(kHeartbeatPrefix.size()));
  }

  constexpr std::string_view kTimeoutPrefix = "timeout/";
  if (stage.rfind(kTimeoutPrefix, 0) == 0) {
    return is_watchdog_metric_token(stage.substr(kTimeoutPrefix.size()));
  }

  constexpr std::string_view kConsecutivePrefix = "consecutive_miss/";
  if (stage.rfind(kConsecutivePrefix, 0) == 0) {
    return is_watchdog_metric_token(stage.substr(kConsecutivePrefix.size()));
  }

  return false;
}

[[nodiscard]] metrics::MetricIdentity make_watchdog_metric_identity(
    WatchdogMetricKind kind);

struct WatchdogMetricSignal {
  WatchdogMetricKind kind = WatchdogMetricKind::EntitiesTotal;
  double value = 0.0;
  std::int64_t ts_unix_ms = 0;
  std::string entity_type;
  std::string entity_id;
  WatchdogTimeoutLevel timeout_level = WatchdogTimeoutLevel::Unspecified;
  std::optional<WatchdogErrorCode> watchdog_error_code;

  [[nodiscard]] bool has_consistent_values() const {
    if (!std::isfinite(value) || value < 0.0 || ts_unix_ms <= 0) {
      return false;
    }

    switch (kind) {
      case WatchdogMetricKind::EntitiesTotal:
        return entity_type.empty() && entity_id.empty() &&
               timeout_level == WatchdogTimeoutLevel::Unspecified &&
               !watchdog_error_code.has_value();
      case WatchdogMetricKind::HeartbeatIngestTotal:
        return is_watchdog_metric_token(entity_type) && entity_id.empty() &&
               timeout_level == WatchdogTimeoutLevel::Unspecified &&
               !watchdog_error_code.has_value();
      case WatchdogMetricKind::TimeoutTotal:
        return is_watchdog_metric_token(entity_type) && entity_id.empty() &&
               timeout_level != WatchdogTimeoutLevel::Unspecified &&
               !watchdog_error_code.has_value();
      case WatchdogMetricKind::ConsecutiveMiss:
        return entity_type.empty() && is_watchdog_metric_token(entity_id) &&
               timeout_level == WatchdogTimeoutLevel::Unspecified &&
               !watchdog_error_code.has_value();
      case WatchdogMetricKind::ScanLagMs:
        return entity_type.empty() && entity_id.empty() &&
               timeout_level == WatchdogTimeoutLevel::Unspecified &&
               (!watchdog_error_code.has_value() ||
                *watchdog_error_code == WatchdogErrorCode::ScanOverdue);
      case WatchdogMetricKind::EventPublishFailTotal:
        return entity_type.empty() && entity_id.empty() &&
               timeout_level == WatchdogTimeoutLevel::Unspecified &&
               (!watchdog_error_code.has_value() ||
                *watchdog_error_code == WatchdogErrorCode::EventPublishFail);
      case WatchdogMetricKind::SafeModeTotal:
        return entity_type.empty() && entity_id.empty() &&
               timeout_level == WatchdogTimeoutLevel::Unspecified &&
               !watchdog_error_code.has_value();
    }

    return false;
  }
};

struct WatchdogMetricsEmitResult {
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

class WatchdogMetricsBridge {
 public:
  explicit WatchdogMetricsBridge(
      std::shared_ptr<metrics::IMetricsProvider> metrics_provider,
      std::string profile_id = "unknown");

  WatchdogMetricsEmitResult record_entities_total(std::uint64_t total_entities,
                                                  std::int64_t ts_unix_ms);
  WatchdogMetricsEmitResult record_heartbeat_ingest(std::string entity_type,
                                                    std::int64_t ts_unix_ms,
                                                    double count = 1.0);
  WatchdogMetricsEmitResult record_timeout(const TimeoutDecision& decision,
                                           std::string entity_type,
                                           std::int64_t ts_unix_ms,
                                           double count = 1.0);
  WatchdogMetricsEmitResult record_consecutive_miss(std::string entity_id,
                                                    std::uint32_t consecutive_miss,
                                                    std::int64_t ts_unix_ms);
  WatchdogMetricsEmitResult record_scan_lag(std::int64_t scan_lag_ms,
                                            std::int64_t ts_unix_ms,
                                            bool overdue = false);
  WatchdogMetricsEmitResult record_publish_fail(std::int64_t ts_unix_ms,
                                                double count = 1.0);
  WatchdogMetricsEmitResult record_safe_mode(std::int64_t ts_unix_ms,
                                             double count = 1.0);

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
  static constexpr std::size_t to_index(WatchdogMetricKind kind) {
    return static_cast<std::size_t>(kind);
  }

  WatchdogMetricsEmitResult emit(const WatchdogMetricSignal& signal);
  bool ensure_meter_ready(WatchdogMetricsEmitResult* failure);
  bool ensure_instruments_registered(WatchdogMetricsEmitResult* failure);
  WatchdogMetricsEmitResult make_failure_result(
      metrics::MetricsErrorCode error_code,
      metrics::MetricsOperationStatus status);
  metrics::MetricSample make_sample(const WatchdogMetricSignal& signal) const;

  std::shared_ptr<metrics::IMetricsProvider> metrics_provider_;
  std::shared_ptr<metrics::IMeter> meter_;
  std::string profile_id_;
  bool degraded_ = false;
  bool instruments_registered_ = false;
  std::array<std::optional<metrics::InstrumentHandle>,
             kWatchdogMetricFamilyCount>
      instrument_handles_{};
  std::uint64_t emission_attempt_total_ = 0;
  std::uint64_t emission_failure_total_ = 0;
  std::optional<metrics::MetricsErrorCode> last_metrics_error_code_;
};

}  // namespace dasall::infra::watchdog