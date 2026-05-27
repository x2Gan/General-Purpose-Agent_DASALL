#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "config/ConfigTypes.h"
#include "logging/ILogConfigurator.h"

namespace dasall::infra {

namespace logging {

class ILogger;

}  // namespace logging

namespace audit {

class IAuditLogger;

}  // namespace audit

namespace metrics {

class IMetricsProvider;

}  // namespace metrics

namespace tracing {

class ITracerProvider;

}  // namespace tracing

class IHealthMonitor;

struct ObservabilityLiveCompositionOptions {
  std::string profile_id = "unknown";
  std::string metrics_granularity = "full";
  double trace_sample_ratio = 0.0;
  std::size_t audit_primary_capacity = 16U;
  std::size_t audit_fallback_capacity = 8U;
  std::string metrics_exporter_type = "noop";
  std::uint32_t metrics_reader_interval_ms = 1000U;
  std::uint32_t metrics_exporter_timeout_ms = 10000U;
  std::string trace_exporter_type = "noop";
  std::string trace_exporter_otlp_endpoint;
  std::uint32_t trace_export_timeout_ms = 100U;
  std::string logging_level = "info";
  bool logging_diag_pull_enabled = true;
  std::vector<config::TypedConfig> logging_config_entries;
  std::filesystem::path logging_state_root_override;
};

struct ObservabilityLiveCompositionResult {
  std::shared_ptr<logging::ILogger> logger;
  std::shared_ptr<audit::IAuditLogger> audit_logger;
  std::shared_ptr<metrics::IMetricsProvider> metrics_provider;
  std::shared_ptr<tracing::ITracerProvider> tracer_provider;
  std::shared_ptr<IHealthMonitor> health_monitor;
  std::optional<logging::LoggingConfig> active_logging_config;
  std::string error;

  [[nodiscard]] bool ok() const {
    return logger != nullptr && audit_logger != nullptr &&
           metrics_provider != nullptr &&
           tracer_provider != nullptr && health_monitor != nullptr &&
           error.empty();
  }
};

[[nodiscard]] ObservabilityLiveCompositionResult compose_live_observability(
    const ObservabilityLiveCompositionOptions& options = {});

}  // namespace dasall::infra