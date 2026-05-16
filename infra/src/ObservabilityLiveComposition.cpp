#include "ObservabilityLiveComposition.h"

#include <algorithm>
#include <memory>
#include <string>

#include "InfraContext.h"
#include "audit/AuditService.h"
#include "health/HealthMonitorFacade.h"
#include "logging/LoggingFacade.h"
#include "metrics/MetricsFacade.h"
#include "tracing/TraceConfig.h"
#include "tracing/TracerProviderImpl.h"

namespace dasall::infra {
namespace {

[[nodiscard]] std::string error_message_for(const InfraOperationResult& result,
                                           std::string_view fallback) {
  if (result.error.has_value() && !result.error->details.message.empty()) {
    return result.error->details.message;
  }

  return std::string(fallback);
}

[[nodiscard]] metrics::MetricsProviderConfig make_metrics_config(
    const ObservabilityLiveCompositionOptions& options) {
  return metrics::MetricsProviderConfig{
      .enabled = true,
      .provider_type = std::string("internal"),
      .exporter_type = options.metrics_exporter_type.empty()
          ? std::string("noop")
          : options.metrics_exporter_type,
      .reader_interval_ms = options.metrics_reader_interval_ms,
      .exporter_timeout_ms = options.metrics_exporter_timeout_ms,
  };
}

[[nodiscard]] tracing::TraceConfig make_trace_config(
    const ObservabilityLiveCompositionOptions& options) {
  tracing::TraceConfig config;
  config.exporter.type = options.trace_exporter_type.empty()
      ? std::string(tracing::kTraceExporterTypeNoop)
      : options.trace_exporter_type;
  config.sampler.type = std::string(tracing::kTraceSamplerTypeRatio);
  config.sampler.ratio = std::clamp(options.trace_sample_ratio, 0.0, 1.0);
  config.batch.enabled = false;
  config.batch.max_queue_size = 32U;
  config.batch.max_export_batch_size = 8U;
  config.batch.schedule_delay_ms = 500U;
  config.export_timeout_ms = options.trace_export_timeout_ms;
  config.force_flush_on_stop = true;
  return config;
}

}  // namespace

ObservabilityLiveCompositionResult compose_live_observability(
    const ObservabilityLiveCompositionOptions& options) {
  auto logger = std::make_shared<logging::LoggingFacade>();
  const auto logger_init = logger->init(logging::LogContext{});
  if (!logger_init.ok) {
    return ObservabilityLiveCompositionResult{
        .logger = nullptr,
        .audit_logger = nullptr,
        .metrics_provider = nullptr,
        .tracer_provider = nullptr,
        .health_monitor = nullptr,
        .error = std::string("logging init failed: ") +
                 error_message_for(logger_init, "infra.logging.init"),
    };
  }

  auto audit_logger = std::make_shared<audit::AuditService>();
  const auto audit_init = audit_logger->init(audit::AuditServiceConfig{
      .primary_capacity = options.audit_primary_capacity,
      .fallback_capacity = options.audit_fallback_capacity,
  });
  if (!audit_init.ok) {
    return ObservabilityLiveCompositionResult{
        .logger = nullptr,
        .audit_logger = nullptr,
        .metrics_provider = nullptr,
        .tracer_provider = nullptr,
        .health_monitor = nullptr,
        .error = std::string("audit init failed: ") +
                 error_message_for(audit_init, "infra.audit.init"),
    };
  }

  const auto audit_start = audit_logger->start();
  if (!audit_start.ok) {
    return ObservabilityLiveCompositionResult{
        .logger = nullptr,
        .audit_logger = nullptr,
        .metrics_provider = nullptr,
        .tracer_provider = nullptr,
        .health_monitor = nullptr,
        .error = std::string("audit start failed: ") +
                 error_message_for(audit_start, "infra.audit.start"),
    };
  }

  auto metrics_provider = std::make_shared<metrics::MetricsFacade>();
  const auto metrics_init = metrics_provider->init(make_metrics_config(options));
  if (!metrics_init.ok) {
    return ObservabilityLiveCompositionResult{
        .logger = nullptr,
        .audit_logger = nullptr,
        .metrics_provider = nullptr,
        .tracer_provider = nullptr,
        .health_monitor = nullptr,
        .error = std::string("metrics init failed: ") + metrics_init.state_ref,
    };
  }

  auto tracer_provider = std::make_shared<tracing::TracerProviderImpl>();
  tracer_provider->set_metrics_provider(metrics_provider, options.profile_id);
  tracer_provider->set_audit_logger(audit_logger, InfraContext{});
  const auto trace_init = tracer_provider->init(make_trace_config(options));
  if (!trace_init.ok) {
    return ObservabilityLiveCompositionResult{
        .logger = nullptr,
        .audit_logger = nullptr,
        .metrics_provider = nullptr,
        .tracer_provider = nullptr,
        .health_monitor = nullptr,
        .error = std::string("trace init failed: ") + trace_init.state_ref,
    };
  }

  return ObservabilityLiveCompositionResult{
      .logger = logger,
      .audit_logger = audit_logger,
      .metrics_provider = metrics_provider,
      .tracer_provider = tracer_provider,
      .health_monitor = std::make_shared<HealthMonitorFacade>(),
      .error = {},
  };
}

}  // namespace dasall::infra