#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "audit/AuditErrors.h"
#include "metrics/IMeter.h"
#include "metrics/IMetricsProvider.h"
#include "metrics/MetricsErrors.h"
#include "metrics/MetricTypes.h"

namespace dasall::infra::audit {

enum class AuditMetricKind {
  WriteTotal = 0,
  WriteFailTotal,
  FallbackTotal,
  FallbackFailTotal,
  ExportTotal,
  ExportFailTotal,
  QueueDepth,
};

inline constexpr std::string_view kAuditMetricsMeterScopeName = "infra.audit";
inline constexpr std::string_view kAuditMetricsMeterScopeVersion = "v1";
inline constexpr std::string_view kAuditMetricModuleLabel = "audit";
inline constexpr std::string_view kAuditMetricNoErrorCodeLabel = "none";
inline constexpr std::array<std::string_view, 5> kAuditMetricAllowedStages{
    "write",
    "fallback",
    "export",
    "retention",
    "health",
};
inline constexpr std::array<std::string_view, 3> kAuditMetricAllowedOutcomes{
    "success",
    "failure",
    "degraded",
};
inline constexpr std::size_t kAuditMetricFamilyCount = 7U;

[[nodiscard]] inline constexpr std::string_view audit_metric_name(
    AuditMetricKind kind) {
  switch (kind) {
    case AuditMetricKind::WriteTotal:
      return "audit_write_total";
    case AuditMetricKind::WriteFailTotal:
      return "audit_write_fail_total";
    case AuditMetricKind::FallbackTotal:
      return "audit_fallback_total";
    case AuditMetricKind::FallbackFailTotal:
      return "audit_fallback_fail_total";
    case AuditMetricKind::ExportTotal:
      return "audit_export_total";
    case AuditMetricKind::ExportFailTotal:
      return "audit_export_fail_total";
    case AuditMetricKind::QueueDepth:
      return "audit_queue_depth";
  }

  return "audit_unknown_metric";
}

[[nodiscard]] inline constexpr metrics::MetricType audit_metric_type(
    AuditMetricKind kind) {
  switch (kind) {
    case AuditMetricKind::QueueDepth:
      return metrics::MetricType::Gauge;
    case AuditMetricKind::WriteTotal:
    case AuditMetricKind::WriteFailTotal:
    case AuditMetricKind::FallbackTotal:
    case AuditMetricKind::FallbackFailTotal:
    case AuditMetricKind::ExportTotal:
    case AuditMetricKind::ExportFailTotal:
      return metrics::MetricType::Counter;
  }

  return metrics::MetricType::Counter;
}

[[nodiscard]] inline constexpr std::string_view audit_metric_unit(
    AuditMetricKind) {
  return "1";
}

[[nodiscard]] inline bool is_audit_metric_stage(std::string_view stage) {
  return std::find(kAuditMetricAllowedStages.begin(),
                   kAuditMetricAllowedStages.end(),
                   stage) != kAuditMetricAllowedStages.end();
}

[[nodiscard]] inline bool is_audit_metric_outcome(std::string_view outcome) {
  return std::find(kAuditMetricAllowedOutcomes.begin(),
                   kAuditMetricAllowedOutcomes.end(),
                   outcome) != kAuditMetricAllowedOutcomes.end();
}

[[nodiscard]] inline bool is_audit_metric_error_code(
  const std::string_view& error_code) {
  return error_code == kAuditMetricNoErrorCodeLabel ||
         error_code == audit_error_code_name(AuditErrorCode::InvalidEvent) ||
         error_code == audit_error_code_name(AuditErrorCode::WriteFail) ||
         error_code == audit_error_code_name(AuditErrorCode::FallbackFail) ||
         error_code == audit_error_code_name(AuditErrorCode::ExportDenied) ||
         error_code == audit_error_code_name(AuditErrorCode::ExportFail) ||
         error_code == audit_error_code_name(AuditErrorCode::RetentionFail);
}

[[nodiscard]] metrics::MetricIdentity make_audit_metric_identity(
    AuditMetricKind kind);

struct AuditMetricSignal {
  AuditMetricKind kind = AuditMetricKind::WriteTotal;
  double value = 0.0;
  std::int64_t ts_unix_ms = 0;
  std::string stage = std::string(kAuditMetricAllowedStages.front());
  std::string outcome = std::string(kAuditMetricAllowedOutcomes.front());
  std::optional<AuditErrorCode> audit_error_code;

  [[nodiscard]] bool has_consistent_values() const {
    if (!std::isfinite(value) || value < 0.0 || ts_unix_ms <= 0 ||
        !is_audit_metric_stage(stage) || !is_audit_metric_outcome(outcome)) {
      return false;
    }

    if (outcome == "success" && audit_error_code.has_value()) {
      return false;
    }

    switch (kind) {
      case AuditMetricKind::WriteTotal:
        return outcome == "success" && !audit_error_code.has_value();
      case AuditMetricKind::WriteFailTotal:
        return outcome == "failure" && audit_error_code.has_value();
      case AuditMetricKind::FallbackTotal:
        return outcome == "degraded" && !audit_error_code.has_value();
      case AuditMetricKind::FallbackFailTotal:
        return outcome == "failure" && audit_error_code.has_value();
      case AuditMetricKind::ExportTotal:
        return outcome == "success" && !audit_error_code.has_value();
      case AuditMetricKind::ExportFailTotal:
        return outcome == "failure" && audit_error_code.has_value();
      case AuditMetricKind::QueueDepth:
        return outcome != "failure" && !audit_error_code.has_value();
    }

    return false;
  }
};

struct AuditMetricsEmitResult {
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

class AuditMetricsBridge {
 public:
  explicit AuditMetricsBridge(
      std::shared_ptr<metrics::IMetricsProvider> metrics_provider,
      std::string profile_id = "unknown");

  AuditMetricsEmitResult emit(const AuditMetricSignal& signal);

  [[nodiscard]] bool is_degraded() const {
    return degraded_;
  }

  [[nodiscard]] bool has_active_meter() const {
    return static_cast<bool>(meter_);
  }

  [[nodiscard]] bool instruments_registered() const {
    return instruments_registered_;
  }

  [[nodiscard]] bool is_noop() const {
    return no_op_mode_;
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
  static constexpr std::size_t to_index(AuditMetricKind kind) {
    return static_cast<std::size_t>(kind);
  }

  bool ensure_meter_ready(AuditMetricsEmitResult* failure);
  bool ensure_instruments_registered(AuditMetricsEmitResult* failure);
  AuditMetricsEmitResult make_failure_result(
      metrics::MetricsErrorCode error_code,
      metrics::MetricsOperationStatus status);
  metrics::MetricSample make_sample(const AuditMetricSignal& signal) const;

  std::shared_ptr<metrics::IMetricsProvider> metrics_provider_;
  std::shared_ptr<metrics::IMeter> meter_;
  std::string profile_id_;
  bool degraded_ = false;
  bool instruments_registered_ = false;
  bool no_op_mode_ = false;
  std::array<std::optional<metrics::InstrumentHandle>,
             kAuditMetricFamilyCount>
      instrument_handles_{};
  std::uint64_t emission_attempt_total_ = 0;
  std::uint64_t emission_failure_total_ = 0;
  std::optional<metrics::MetricsErrorCode> last_metrics_error_code_;
};

}  // namespace dasall::infra::audit
