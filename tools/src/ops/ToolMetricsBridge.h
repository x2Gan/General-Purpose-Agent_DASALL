#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "ToolInvocationContext.h"
#include "ToolInvocationEnvelope.h"
#include "metrics/IMeter.h"
#include "metrics/MetricsErrors.h"
#include "route/ToolRouteSelector.h"
#include "tool/ToolDescriptor.h"
#include "tool/ToolRequest.h"

namespace dasall::tools::ops {

enum class ToolMetricKind {
  request_total = 0,
  admission_denied_total,
  execution_latency_ms,
  partial_side_effect_total,
  mcp_stale_snapshot_total,
  workflow_step_failure_total,
};

inline constexpr std::size_t kToolMetricFamilyCount = 6U;
inline constexpr std::string_view kToolMetricNoErrorCodeLabel = "none";
inline constexpr std::string_view kToolMetricsMeterScopeName = "tools";
inline constexpr std::string_view kToolMetricsMeterScopeVersion = "v1";

struct ToolMetricsBridgeOptions {
  bool enabled = true;
  std::string profile_id = "unknown";
  std::string metrics_granularity = "full";
  std::string meter_scope_name = std::string(kToolMetricsMeterScopeName);
  std::string meter_scope_version = std::string(kToolMetricsMeterScopeVersion);
  std::function<std::int64_t()> now_ms;
};

struct ToolMetricsEmitResult {
  bool emitted = false;
  bool bridge_degraded = false;
  bool signal_suppressed = false;
  infra::metrics::MetricsOperationStatus status =
      infra::metrics::MetricsOperationStatus::success("metrics://tools/idle");
  std::optional<infra::metrics::MetricsErrorCode> metrics_error_code;

  [[nodiscard]] bool references_only_contract_error_types() const {
    return status.references_only_contract_error_types();
  }

  [[nodiscard]] bool has_consistent_state() const {
    if (emitted) {
      return status.ok && !bridge_degraded && !signal_suppressed &&
             !metrics_error_code.has_value() && references_only_contract_error_types();
    }

    if (status.ok) {
      return !bridge_degraded && !metrics_error_code.has_value() &&
             references_only_contract_error_types();
    }

    return metrics_error_code.has_value() && references_only_contract_error_types();
  }
};

class ToolMetricsBridge {
 public:
  explicit ToolMetricsBridge(
      std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider = nullptr,
      ToolMetricsBridgeOptions options = {});

  void set_metrics_provider(
      std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider,
      std::string profile_id = {});

  [[nodiscard]] ToolMetricsEmitResult record_preflight_failure(
      const contracts::ToolRequest& request,
      const std::optional<contracts::ToolDescriptor>& descriptor,
      const ToolInvocationEnvelope& envelope,
      const ToolInvocationContext& context);
  [[nodiscard]] ToolMetricsEmitResult record_admission_denied(
      const contracts::ToolRequest& request,
      const std::optional<contracts::ToolDescriptor>& descriptor,
      std::string_view reason_code,
      const ToolInvocationContext& context);
  [[nodiscard]] ToolMetricsEmitResult record_route_selection(
      const contracts::ToolRequest& request,
      const std::optional<contracts::ToolDescriptor>& descriptor,
      const route::ToolRouteDecision& route_decision,
      const ToolInvocationContext& context);
  [[nodiscard]] ToolMetricsEmitResult record_execution_terminal(
      const contracts::ToolRequest& request,
      const std::optional<contracts::ToolDescriptor>& descriptor,
      const ToolInvocationEnvelope& envelope,
      const ToolInvocationContext& context);
  [[nodiscard]] ToolMetricsEmitResult record_workflow_step_failure(
      std::string_view workflow_id,
      std::string_view step_id,
      std::string_view failure_type,
      const ToolInvocationContext& context);

  [[nodiscard]] bool has_active_meter() const {
    return static_cast<bool>(meter_);
  }

  [[nodiscard]] bool instruments_registered() const {
    return instruments_registered_;
  }

  [[nodiscard]] bool is_degraded() const {
    return degraded_;
  }

  [[nodiscard]] std::uint64_t emission_attempt_total() const {
    return emission_attempt_total_;
  }

  [[nodiscard]] std::uint64_t emission_failure_total() const {
    return emission_failure_total_;
  }

  [[nodiscard]] const std::optional<infra::metrics::MetricsErrorCode>&
  last_metrics_error_code() const {
    return last_metrics_error_code_;
  }

 private:
  struct ToolMetricSignal;

  [[nodiscard]] bool is_enabled() const;
  [[nodiscard]] bool granularity_allows(ToolMetricKind kind) const;
  [[nodiscard]] std::int64_t current_time_unix_ms() const;
  [[nodiscard]] ToolMetricsEmitResult emit_signal(const ToolMetricSignal& signal);
  [[nodiscard]] bool ensure_meter_ready(ToolMetricsEmitResult* failure);
  [[nodiscard]] bool ensure_instruments_registered(ToolMetricsEmitResult* failure);
  [[nodiscard]] ToolMetricsEmitResult make_failure_result(
      infra::metrics::MetricsErrorCode error_code,
      infra::metrics::MetricsOperationStatus status);

  std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider_;
  ToolMetricsBridgeOptions options_{};
  std::shared_ptr<infra::metrics::IMeter> meter_;
  std::array<std::optional<infra::metrics::InstrumentHandle>,
             kToolMetricFamilyCount>
      instrument_handles_{};
  bool instruments_registered_ = false;
  bool degraded_ = false;
  std::uint64_t emission_attempt_total_ = 0;
  std::uint64_t emission_failure_total_ = 0;
  std::optional<infra::metrics::MetricsErrorCode> last_metrics_error_code_;
};

}  // namespace dasall::tools::ops