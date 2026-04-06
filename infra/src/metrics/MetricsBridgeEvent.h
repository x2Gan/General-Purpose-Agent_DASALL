#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "InfraContext.h"
#include "metrics/MetricsErrors.h"
#include "metrics/MetricsSnapshots.h"

namespace dasall::infra::metrics {

enum class MetricsBridgeEventKind {
  RecoveryTransition = 0,
  ConfigChange,
};

enum class MetricsBridgeEventOutcome {
  Success = 0,
  Failure,
  Degraded,
};

inline constexpr std::string_view kMetricsBridgeDefaultWorkerType =
    "infra.metrics";
inline constexpr std::array<std::string_view, 4>
    kMetricsRecoveryBridgeActions{
        "enter_degraded",
        "degraded_still_active",
        "recover_to_healthy",
        "healthy_still_active",
    };
inline constexpr std::array<std::string_view, 3> kMetricsConfigBridgeActions{
    "config_changed",
    "config_rollback",
    "histogram_buckets_changed",
};

[[nodiscard]] inline constexpr std::string_view metrics_bridge_event_kind_name(
    MetricsBridgeEventKind kind) {
  switch (kind) {
    case MetricsBridgeEventKind::RecoveryTransition:
      return "recovery";
    case MetricsBridgeEventKind::ConfigChange:
      return "config";
  }

  return "unknown";
}

[[nodiscard]] inline constexpr std::string_view metrics_bridge_event_outcome_name(
    MetricsBridgeEventOutcome outcome) {
  switch (outcome) {
    case MetricsBridgeEventOutcome::Success:
      return "success";
    case MetricsBridgeEventOutcome::Failure:
      return "failure";
    case MetricsBridgeEventOutcome::Degraded:
      return "degraded";
  }

  return "unknown";
}

[[nodiscard]] inline bool is_metrics_recovery_bridge_action(
    std::string_view action) {
  return std::find(kMetricsRecoveryBridgeActions.begin(),
                   kMetricsRecoveryBridgeActions.end(),
                   action) != kMetricsRecoveryBridgeActions.end();
}

[[nodiscard]] inline bool is_metrics_config_bridge_action(
    std::string_view action) {
  return std::find(kMetricsConfigBridgeActions.begin(),
                   kMetricsConfigBridgeActions.end(),
                   action) != kMetricsConfigBridgeActions.end();
}

struct MetricsBridgeContext {
  InfraContext infra_context{};
  std::string worker_type = std::string(kMetricsBridgeDefaultWorkerType);

  [[nodiscard]] bool is_valid() const {
    return !infra_context.request_id.empty() && !infra_context.session_id.empty() &&
           !infra_context.trace_id.empty() && !infra_context.task_id.empty() &&
           !infra_context.parent_task_id.empty() && !infra_context.lease_id.empty() &&
           !worker_type.empty();
  }
};

struct MetricsBridgeEvent {
  MetricsBridgeEventKind kind = MetricsBridgeEventKind::RecoveryTransition;
  std::string action;
  std::string stage;
  MetricsBridgeEventOutcome outcome = MetricsBridgeEventOutcome::Success;
  std::string reason;
  std::optional<MetricsErrorCode> error_code;
  MetricsModuleSnapshot module_snapshot{};
  MetricsBridgeContext context{};
  std::string detail_ref;
  std::string config_version;
  std::string previous_config_version;
  std::uint64_t consecutive_failure_total = 0;
  std::uint64_t degrade_enter_total = 0;
  std::uint64_t recovery_success_total = 0;
  std::int64_t timestamp_ms = 0;

  [[nodiscard]] bool is_valid() const {
    if (action.empty() || stage.empty() || reason.empty() || detail_ref.empty() ||
        timestamp_ms <= 0 || !module_snapshot.is_valid() || !context.is_valid()) {
      return false;
    }

    if (outcome == MetricsBridgeEventOutcome::Success && error_code.has_value()) {
      return false;
    }

    if (outcome == MetricsBridgeEventOutcome::Failure && !error_code.has_value()) {
      return false;
    }

    switch (kind) {
      case MetricsBridgeEventKind::RecoveryTransition:
        if (!is_metrics_recovery_bridge_action(action) ||
            !config_version.empty() || !previous_config_version.empty()) {
          return false;
        }

        if ((action == "enter_degraded" || action == "degraded_still_active") &&
            !error_code.has_value()) {
          return false;
        }

        if ((action == "recover_to_healthy" || action == "healthy_still_active") &&
            error_code.has_value()) {
          return false;
        }

        return true;

      case MetricsBridgeEventKind::ConfigChange:
        if (!is_metrics_config_bridge_action(action) || config_version.empty()) {
          return false;
        }

        if (action == "config_changed" ||
            action == "histogram_buckets_changed") {
          return outcome == MetricsBridgeEventOutcome::Success &&
                 !previous_config_version.empty();
        }

        if (action == "config_rollback") {
          return !previous_config_version.empty() && error_code.has_value() &&
                 outcome != MetricsBridgeEventOutcome::Success;
        }

        return false;
    }

    return false;
  }
};

}  // namespace dasall::infra::metrics