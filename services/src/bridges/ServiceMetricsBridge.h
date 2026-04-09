#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "ServiceTypes.h"
#include "metrics/IMeter.h"
#include "metrics/MetricsErrors.h"

namespace dasall::services::internal {

enum class ServiceMetricKind {
  execution_requests_total = 0,
  execution_latency_ms,
  execution_circuit_open_total,
  data_query_requests_total,
  subscription_overflow_total,
  compensation_hint_total,
};

inline constexpr std::size_t kServiceMetricFamilyCount = 6U;
inline constexpr std::string_view kServiceMetricNoErrorCodeLabel = "none";
inline constexpr std::string_view kServiceMetricsMeterScopeName = "services";
inline constexpr std::string_view kServiceMetricsMeterScopeVersion = "v1";

struct ServiceMetricsBridgeOptions {
  bool enabled = true;
  std::string profile_id = "unknown";
  std::string metrics_granularity = "full";
  std::string meter_scope_name = std::string(kServiceMetricsMeterScopeName);
  std::string meter_scope_version = std::string(kServiceMetricsMeterScopeVersion);
  std::function<std::int64_t()> now_ms;
};

struct ServiceMetricsEmitResult {
  bool emitted = false;
  bool bridge_degraded = false;
  bool signal_suppressed = false;
  infra::metrics::MetricsOperationStatus status =
      infra::metrics::MetricsOperationStatus::success("metrics://services/idle");
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

class ServiceMetricsBridge {
 public:
  explicit ServiceMetricsBridge(
      std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider = nullptr,
      ServiceMetricsBridgeOptions options = {});

  void set_metrics_provider(
      std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider,
      std::string profile_id = {});

  [[nodiscard]] ServiceMetricsEmitResult record_execution_result(
      std::string_view action,
      std::string_view adapter_id,
      const ExecutionCommandResult& result,
      std::optional<std::uint64_t> latency_ms = std::nullopt);
  [[nodiscard]] ServiceMetricsEmitResult record_execution_query_result(
      std::string_view query_kind,
      std::string_view adapter_id,
      const ExecutionQueryResult& result,
      std::optional<std::uint64_t> latency_ms = std::nullopt);
  [[nodiscard]] ServiceMetricsEmitResult record_execution_circuit_open(
      std::string_view action,
      std::string_view adapter_id,
      std::string_view reason = "circuit_open");
  [[nodiscard]] ServiceMetricsEmitResult record_data_query_result(
      std::string_view query_kind,
      const DataQueryResult& result,
      std::optional<std::uint64_t> latency_ms = std::nullopt);
  [[nodiscard]] ServiceMetricsEmitResult record_data_catalog_result(
      std::string_view target_class,
      const DataCatalogResult& result,
      std::optional<std::uint64_t> latency_ms = std::nullopt);
  [[nodiscard]] ServiceMetricsEmitResult record_subscription_result(
      std::string_view capability_id,
      std::string_view stream_kind,
      const ExecutionSubscriptionResult& result);

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
  struct ServiceMetricSignal;

  [[nodiscard]] bool is_enabled() const;
  [[nodiscard]] bool granularity_allows(ServiceMetricKind kind) const;
  [[nodiscard]] std::int64_t current_time_unix_ms() const;
  [[nodiscard]] ServiceMetricsEmitResult emit_signal(const ServiceMetricSignal& signal);
  [[nodiscard]] bool ensure_meter_ready(ServiceMetricsEmitResult* failure);
  [[nodiscard]] bool ensure_instruments_registered(ServiceMetricsEmitResult* failure);
  [[nodiscard]] ServiceMetricsEmitResult make_failure_result(
      infra::metrics::MetricsErrorCode error_code,
      infra::metrics::MetricsOperationStatus status);

  std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider_;
  ServiceMetricsBridgeOptions options_{};
  std::shared_ptr<infra::metrics::IMeter> meter_;
  std::array<std::optional<infra::metrics::InstrumentHandle>,
             kServiceMetricFamilyCount>
      instrument_handles_{};
  bool instruments_registered_ = false;
  bool degraded_ = false;
  std::uint64_t emission_attempt_total_ = 0;
  std::uint64_t emission_failure_total_ = 0;
  std::optional<infra::metrics::MetricsErrorCode> last_metrics_error_code_;
};

}  // namespace dasall::services::internal