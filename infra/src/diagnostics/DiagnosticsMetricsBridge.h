#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "diagnostics/DiagnosticsErrors.h"
#include "metrics/IMeter.h"
#include "metrics/IMetricsProvider.h"
#include "metrics/MetricsErrors.h"
#include "metrics/MetricTypes.h"

namespace dasall::infra::diagnostics {

enum class DiagnosticsMetricKind {
  CommandTotal = 0,
  CommandDeniedTotal,
  ExecLatencyMs,
  SnapshotStoreFailTotal,
  ExportTotal,
  RedactionFailTotal,
  SafeModeEnterTotal,
};

inline constexpr std::string_view kDiagnosticsMetricsMeterScopeName = "infra.diagnostics";
inline constexpr std::string_view kDiagnosticsMetricsMeterScopeVersion = "v1";
inline constexpr std::string_view kDiagnosticsMetricModuleLabel = "diagnostics";
inline constexpr std::string_view kDiagnosticsMetricNoErrorCodeLabel = "none";
inline constexpr std::string_view kDiagnosticsMetricDeniedReasonLabel = "diag_command_denied";
inline constexpr std::array<std::string_view, 9> kDiagnosticsMetricAllowedStages{
    "execute.health_snapshot",
    "execute.queue_stats",
    "execute.thread_dump",
    "authorize",
    "redaction",
    "store",
    "export.local_file",
    "export.remote_upload",
    "safe_mode",
};
inline constexpr std::array<std::string_view, 4> kDiagnosticsMetricAllowedOutcomes{
    "success",
    "failure",
    "rejected",
    "degraded",
};
inline constexpr std::size_t kDiagnosticsMetricFamilyCount = 7U;

[[nodiscard]] inline constexpr std::string_view diagnostics_metric_name(
    DiagnosticsMetricKind kind) {
  switch (kind) {
    case DiagnosticsMetricKind::CommandTotal:
      return "infra_diag_command_total";
    case DiagnosticsMetricKind::CommandDeniedTotal:
      return "infra_diag_command_denied_total";
    case DiagnosticsMetricKind::ExecLatencyMs:
      return "infra_diag_exec_latency_ms";
    case DiagnosticsMetricKind::SnapshotStoreFailTotal:
      return "infra_diag_snapshot_store_fail_total";
    case DiagnosticsMetricKind::ExportTotal:
      return "infra_diag_export_total";
    case DiagnosticsMetricKind::RedactionFailTotal:
      return "infra_diag_redaction_fail_total";
    case DiagnosticsMetricKind::SafeModeEnterTotal:
      return "infra_diag_safe_mode_enter_total";
  }

  return "infra_diag_unknown_metric";
}

[[nodiscard]] inline constexpr metrics::MetricType diagnostics_metric_type(
    DiagnosticsMetricKind kind) {
  switch (kind) {
    case DiagnosticsMetricKind::ExecLatencyMs:
      return metrics::MetricType::Histogram;
    case DiagnosticsMetricKind::CommandTotal:
    case DiagnosticsMetricKind::CommandDeniedTotal:
    case DiagnosticsMetricKind::SnapshotStoreFailTotal:
    case DiagnosticsMetricKind::ExportTotal:
    case DiagnosticsMetricKind::RedactionFailTotal:
    case DiagnosticsMetricKind::SafeModeEnterTotal:
      return metrics::MetricType::Counter;
  }

  return metrics::MetricType::Counter;
}

[[nodiscard]] inline constexpr std::string_view diagnostics_metric_unit(
    DiagnosticsMetricKind kind) {
  switch (kind) {
    case DiagnosticsMetricKind::ExecLatencyMs:
      return "ms";
    case DiagnosticsMetricKind::CommandTotal:
    case DiagnosticsMetricKind::CommandDeniedTotal:
    case DiagnosticsMetricKind::SnapshotStoreFailTotal:
    case DiagnosticsMetricKind::ExportTotal:
    case DiagnosticsMetricKind::RedactionFailTotal:
    case DiagnosticsMetricKind::SafeModeEnterTotal:
      return "1";
  }

  return "1";
}

[[nodiscard]] inline bool is_diagnostics_metric_stage(std::string_view stage) {
  return std::find(kDiagnosticsMetricAllowedStages.begin(),
                   kDiagnosticsMetricAllowedStages.end(),
                   stage) != kDiagnosticsMetricAllowedStages.end();
}

[[nodiscard]] inline bool is_diagnostics_metric_outcome(std::string_view outcome) {
  return std::find(kDiagnosticsMetricAllowedOutcomes.begin(),
                   kDiagnosticsMetricAllowedOutcomes.end(),
                   outcome) != kDiagnosticsMetricAllowedOutcomes.end();
}

[[nodiscard]] inline bool is_diagnostics_execute_stage(std::string_view stage) {
  return stage == "execute.health_snapshot" || stage == "execute.queue_stats" ||
         stage == "execute.thread_dump";
}

[[nodiscard]] inline bool is_diagnostics_export_stage(std::string_view stage) {
  return stage == "export.local_file" || stage == "export.remote_upload";
}

[[nodiscard]] inline bool is_diagnostics_metric_error_code(
    std::string_view error_code) {
  return error_code == kDiagnosticsMetricNoErrorCodeLabel ||
         error_code == kDiagnosticsMetricDeniedReasonLabel ||
         error_code == diagnostics_error_code_name(DiagnosticsErrorCode::CommandDenied) ||
         error_code == diagnostics_error_code_name(DiagnosticsErrorCode::CommandInvalid) ||
         error_code == diagnostics_error_code_name(DiagnosticsErrorCode::ExecTimeout) ||
         error_code == diagnostics_error_code_name(DiagnosticsErrorCode::ExecFail) ||
         error_code == diagnostics_error_code_name(DiagnosticsErrorCode::RedactionFail) ||
         error_code == diagnostics_error_code_name(DiagnosticsErrorCode::SnapshotStoreFail) ||
         error_code == diagnostics_error_code_name(DiagnosticsErrorCode::ExportFail) ||
         error_code == diagnostics_error_code_name(DiagnosticsErrorCode::RemoteExportDisabled);
}

[[nodiscard]] metrics::MetricIdentity make_diagnostics_metric_identity(
    DiagnosticsMetricKind kind);

struct DiagnosticsMetricSignal {
  DiagnosticsMetricKind kind = DiagnosticsMetricKind::CommandTotal;
  double value = 0.0;
  std::int64_t ts_unix_ms = 0;
  std::string stage = std::string(kDiagnosticsMetricAllowedStages.front());
  std::string outcome = std::string(kDiagnosticsMetricAllowedOutcomes.front());
  std::string error_code = std::string(kDiagnosticsMetricNoErrorCodeLabel);

  [[nodiscard]] bool has_consistent_values() const {
    if (!std::isfinite(value) || value < 0.0 || ts_unix_ms <= 0 ||
        !is_diagnostics_metric_stage(stage) ||
        !is_diagnostics_metric_outcome(outcome) ||
        !is_diagnostics_metric_error_code(error_code)) {
      return false;
    }

    if (outcome == "success" && error_code != kDiagnosticsMetricNoErrorCodeLabel) {
      return false;
    }

    switch (kind) {
      case DiagnosticsMetricKind::CommandTotal:
        return is_diagnostics_execute_stage(stage) &&
               (outcome == "success" || outcome == "failure" || outcome == "rejected");
      case DiagnosticsMetricKind::CommandDeniedTotal:
        return stage == "authorize" && outcome == "rejected" &&
               error_code == kDiagnosticsMetricDeniedReasonLabel;
      case DiagnosticsMetricKind::ExecLatencyMs:
        return is_diagnostics_execute_stage(stage) &&
               (outcome == "success" || outcome == "failure");
      case DiagnosticsMetricKind::SnapshotStoreFailTotal:
        return stage == "store" && outcome == "failure" &&
               error_code == diagnostics_error_code_name(DiagnosticsErrorCode::SnapshotStoreFail);
      case DiagnosticsMetricKind::ExportTotal:
        return is_diagnostics_export_stage(stage) &&
               (outcome == "success" || outcome == "failure" || outcome == "rejected");
      case DiagnosticsMetricKind::RedactionFailTotal:
        return stage == "redaction" && outcome == "failure" &&
               error_code == diagnostics_error_code_name(DiagnosticsErrorCode::RedactionFail);
      case DiagnosticsMetricKind::SafeModeEnterTotal:
        return stage == "safe_mode" && outcome == "degraded" &&
               error_code == kDiagnosticsMetricNoErrorCodeLabel;
    }

    return false;
  }
};

struct DiagnosticsMetricsEmitResult {
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

class DiagnosticsMetricsBridge {
 public:
  explicit DiagnosticsMetricsBridge(
      std::shared_ptr<metrics::IMetricsProvider> metrics_provider,
      std::string profile_id = "unknown");

  DiagnosticsMetricsEmitResult emit(const DiagnosticsMetricSignal& signal);

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
  static constexpr std::size_t to_index(DiagnosticsMetricKind kind) {
    return static_cast<std::size_t>(kind);
  }

  bool ensure_meter_ready(DiagnosticsMetricsEmitResult* failure);
  bool ensure_instruments_registered(DiagnosticsMetricsEmitResult* failure);
  DiagnosticsMetricsEmitResult make_failure_result(
      metrics::MetricsErrorCode error_code,
      metrics::MetricsOperationStatus status);
  metrics::MetricSample make_sample(const DiagnosticsMetricSignal& signal) const;

  std::shared_ptr<metrics::IMetricsProvider> metrics_provider_;
  std::shared_ptr<metrics::IMeter> meter_;
  std::string profile_id_;
  bool degraded_ = false;
  bool instruments_registered_ = false;
  std::array<std::optional<metrics::InstrumentHandle>,
             kDiagnosticsMetricFamilyCount>
      instrument_handles_{};
  std::uint64_t emission_attempt_total_ = 0;
  std::uint64_t emission_failure_total_ = 0;
  std::optional<metrics::MetricsErrorCode> last_metrics_error_code_;
};

}  // namespace dasall::infra::diagnostics