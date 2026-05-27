#include "ObservabilityLiveComposition.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "InfraContext.h"
#include "config/IConfigCenter.h"
#include "audit/AuditService.h"
#include "health/HealthMonitorFacade.h"
#include "logging/FileLogSink.h"
#include "logging/LoggingConfigAdapter.h"
#include "logging/LoggingFacade.h"
#include "logging/LoggingHealthProbe.h"
#include "logging/LoggingMetricsBridge.h"
#include "logging/SinkDispatcher.h"
#include "metrics/MetricsFacade.h"
#include "tracing/TraceConfig.h"
#include "tracing/TracerProviderImpl.h"

namespace dasall::infra {
namespace {

constexpr std::string_view kObservabilityConfigSourceIdPrefix = "profiles/";
constexpr std::string_view kObservabilityFallbackSourceId = "config://observability";
constexpr std::string_view kDirectSinkDispatcherSourceRef = "DirectSinkDispatcher";

class StaticLoggingConfigCenter final : public config::IConfigCenter {
 public:
  explicit StaticLoggingConfigCenter(std::vector<config::TypedConfig> entries) {
    for (auto& entry : entries) {
      entries_.insert_or_assign(entry.key_path, std::move(entry));
    }
  }

  config::ConfigApplyResult load_layers(const config::ConfigStartupContext&) override {
    return config::ConfigApplyResult::success("rollback://logging-live/1");
  }

  std::optional<config::TypedConfig> get_typed(
      const config::ConfigQuery& query) const override {
    if (!query.is_valid()) {
      return std::nullopt;
    }

    const auto it = entries_.find(query.key_path);
    if (it != entries_.end() && it->second.value_type == query.expected_type) {
      return it->second;
    }

    if (query.default_policy != config::ConfigDefaultPolicy::ReturnFallback) {
      return std::nullopt;
    }

    return config::TypedConfig{
        .key_path = query.key_path,
        .value_type = query.expected_type,
        .serialized_value = query.fallback_serialized_value,
        .schema_version = std::string(config::kConfigSchemaVersionV1),
        .source_kind = config::ConfigSourceKind::Defaults,
        .source_id = std::string(kObservabilityFallbackSourceId),
        .secret_backed = false,
    };
  }

  config::ConfigApplyResult apply_override(const config::ConfigPatch&) override {
    return config::ConfigApplyResult::success("rollback://logging-live/2");
  }

  config::ConfigApplyResult rollback(const config::ConfigRollbackToken&) override {
    return config::ConfigApplyResult::success("rollback://logging-live/3");
  }

  std::optional<config::ConfigSubscriptionHandle> subscribe(
      const config::ConfigSubscriptionRequest&) override {
    return std::nullopt;
  }

 private:
  std::map<std::string, config::TypedConfig> entries_;
};

class DirectSinkDispatcher final : public logging::ILogDispatchBackend {
 public:
  DirectSinkDispatcher(std::shared_ptr<logging::ILogSink> basic_sink,
                       std::shared_ptr<logging::ILogSink> audit_sink)
      : basic_sink_(std::move(basic_sink)), audit_sink_(std::move(audit_sink)) {}

  logging::LogWriteResult dispatch(const logging::LogEvent& event) override {
    if (!event.attrs_are_serializable()) {
      return logging::LogWriteResult::failure(
          contracts::ResultCode::ValidationFieldMissing,
          "direct sink dispatcher requires serializable log attrs",
          "logging.dispatch",
          std::string(kDirectSinkDispatcherSourceRef));
    }

    if (const auto sink = sink_for_route(select_route(event)); sink != nullptr) {
      return sink->write(event);
    }

    return logging::LogWriteResult::success();
  }

  logging::LogWriteResult flush(const logging::LogFlushDeadline& deadline) override {
    if (basic_sink_ != nullptr) {
      const auto basic_result = basic_sink_->flush(deadline);
      if (!basic_result.ok) {
        return basic_result;
      }
    }

    if (audit_sink_ != nullptr && audit_sink_ != basic_sink_) {
      const auto audit_result = audit_sink_->flush(deadline);
      if (!audit_result.ok) {
        return audit_result;
      }
    }

    return logging::LogWriteResult::success();
  }

 private:
  [[nodiscard]] static bool has_audit_link_attr(const logging::LogEvent& event) {
    return event.attrs.contains("audit_ref") ||
           event.attrs.contains("audit_ref_pending") ||
           event.attrs.contains("evidence_ref");
  }

  [[nodiscard]] static logging::SinkRoute select_route(const logging::LogEvent& event) {
    if (event.category() == "audit" || has_audit_link_attr(event)) {
      return logging::SinkRoute::Audit;
    }

    return logging::SinkRoute::BasicFile;
  }

  [[nodiscard]] std::shared_ptr<logging::ILogSink> sink_for_route(
      logging::SinkRoute route) const {
    if (route == logging::SinkRoute::Audit) {
      return audit_sink_ != nullptr ? audit_sink_ : basic_sink_;
    }

    return basic_sink_;
  }

  std::shared_ptr<logging::ILogSink> basic_sink_;
  std::shared_ptr<logging::ILogSink> audit_sink_;
};

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
  config.exporter.otlp_endpoint = options.trace_exporter_otlp_endpoint;
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

[[nodiscard]] bool has_logging_entry(const std::vector<config::TypedConfig>& entries,
                                     std::string_view key_path) {
  return std::any_of(entries.begin(), entries.end(), [&](const config::TypedConfig& entry) {
    return entry.key_path == key_path;
  });
}

[[nodiscard]] config::TypedConfig make_logging_typed_config(
    std::string key_path,
    config::ConfigValueType value_type,
    std::string serialized_value,
    config::ConfigSourceKind source_kind,
    std::string source_id) {
  return config::TypedConfig{
      .key_path = std::move(key_path),
      .value_type = value_type,
      .serialized_value = std::move(serialized_value),
      .schema_version = std::string(config::kConfigSchemaVersionV1),
      .source_kind = source_kind,
      .source_id = std::move(source_id),
      .secret_backed = false,
  };
}

[[nodiscard]] std::vector<config::TypedConfig> build_logging_config_entries(
    const ObservabilityLiveCompositionOptions& options) {
  auto entries = options.logging_config_entries;
  const std::string profile_source_id =
      std::string(kObservabilityConfigSourceIdPrefix) + options.profile_id +
      "/runtime_policy.yaml";

  if (!has_logging_entry(entries, "infra.logging.level")) {
    entries.push_back(make_logging_typed_config(
        "infra.logging.level",
        config::ConfigValueType::String,
        options.logging_level.empty() ? std::string("info") : options.logging_level,
        config::ConfigSourceKind::Profile,
        profile_source_id));
  }

  if (!has_logging_entry(entries, "infra.logging.export.enable_diag_pull")) {
    entries.push_back(make_logging_typed_config(
        "infra.logging.export.enable_diag_pull",
        config::ConfigValueType::Boolean,
        options.logging_diag_pull_enabled ? std::string("true") : std::string("false"),
        config::ConfigSourceKind::Profile,
        profile_source_id));
  }

  if (!has_logging_entry(entries, "infra.audit.required")) {
    entries.push_back(make_logging_typed_config(
        "infra.audit.required",
        config::ConfigValueType::Boolean,
        std::string("true"),
        config::ConfigSourceKind::Profile,
        profile_source_id));
  }

  return entries;
}

[[nodiscard]] logging::AsyncQueueOptions make_async_queue_options(
    const logging::LoggingConfig& config) {
  return logging::AsyncQueueOptions{
      .capacity = config.queue_size,
      .overflow_policy =
          config.overflow_policy == logging::LoggingOverflowPolicy::OverrunOldest
              ? logging::AsyncQueueOverflowPolicy::OverrunOldest
              : logging::AsyncQueueOverflowPolicy::Block,
  };
}

[[nodiscard]] logging::FileLogSinkOptions make_file_log_sink_options(
    const logging::LoggingConfig& config,
    const ObservabilityLiveCompositionOptions& options) {
  return logging::FileLogSinkOptions{
      .file_path = std::filesystem::path(config.file_path),
      .state_root_override = options.logging_state_root_override,
      .rotate_max_size_bytes =
          static_cast<std::size_t>(config.rotate_max_size_mb) * 1024U * 1024U,
      .rotate_max_files = static_cast<std::size_t>(config.rotate_max_files),
      .path_policy = options.logging_state_root_override.empty()
          ? logging::FileLogPathPolicy::BuildTreeDefault
          : logging::FileLogPathPolicy::InstalledAuthoritative,
  };
}

[[nodiscard]] std::unique_ptr<logging::ILogDispatchBackend> make_logging_dispatch_backend(
    const logging::LoggingConfig& config,
    const ObservabilityLiveCompositionOptions& options) {
  auto basic_sink = std::make_shared<logging::FileLogSink>(
      make_file_log_sink_options(config, options));
  auto audit_sink = basic_sink;

  if (config.async_enabled) {
    return std::make_unique<logging::SinkDispatcher>(logging::SinkDispatcherOptions{
        .queue_options = make_async_queue_options(config),
        .basic_sink = std::move(basic_sink),
        .audit_sink = std::move(audit_sink),
    });
  }

  return std::make_unique<DirectSinkDispatcher>(std::move(basic_sink),
                                                std::move(audit_sink));
}

}  // namespace

ObservabilityLiveCompositionResult compose_live_observability(
    const ObservabilityLiveCompositionOptions& options) {
  const StaticLoggingConfigCenter logging_config_center(
      build_logging_config_entries(options));
  logging::LoggingConfigAdapter logging_config_adapter(logging_config_center);
  const auto logging_config_result = logging_config_adapter.load_and_apply();
  if (!logging_config_result.applied || !logging_config_adapter.has_active_config()) {
    return ObservabilityLiveCompositionResult{
        .logger = nullptr,
        .audit_logger = nullptr,
        .metrics_provider = nullptr,
        .tracer_provider = nullptr,
        .health_monitor = nullptr,
        .active_logging_config = std::nullopt,
        .error = std::string("logging config apply failed: ") +
                 (logging_config_result.error_info.has_value()
                      ? logging_config_result.error_info->details.message
                      : std::string("infra.logging.config")),
    };
  }

  const auto active_logging_config = logging_config_adapter.active_config();
  auto logger = std::make_shared<logging::LoggingFacade>(
      make_logging_dispatch_backend(active_logging_config, options));
  const auto logger_init = logger->init(logging::LogContext{});
  if (!logger_init.ok) {
    return ObservabilityLiveCompositionResult{
        .logger = nullptr,
        .audit_logger = nullptr,
        .metrics_provider = nullptr,
        .tracer_provider = nullptr,
        .health_monitor = nullptr,
        .active_logging_config = std::nullopt,
        .error = std::string("logging init failed: ") +
                 error_message_for(logger_init, "infra.logging.init"),
    };
  }

  const auto logger_config_apply = logger->apply_config(active_logging_config);
  if (!logger_config_apply.ok) {
    return ObservabilityLiveCompositionResult{
        .logger = nullptr,
        .audit_logger = nullptr,
        .metrics_provider = nullptr,
        .tracer_provider = nullptr,
        .health_monitor = nullptr,
        .active_logging_config = std::nullopt,
        .error = std::string("logging config projection failed: ") +
                 error_message_for(logger_config_apply,
                                   "infra.logging.config_projection"),
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
        .active_logging_config = std::nullopt,
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
        .active_logging_config = std::nullopt,
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
        .active_logging_config = std::nullopt,
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
        .active_logging_config = std::nullopt,
        .error = std::string("trace init failed: ") + trace_init.state_ref,
    };
  }

      auto health_monitor = std::make_shared<HealthMonitorFacade>();
      auto logging_metrics_bridge = std::make_shared<logging::LoggingMetricsBridge>(
        metrics_provider,
        options.profile_id);
      logger->attach_metrics_bridge(
        logging_metrics_bridge,
        active_logging_config.async_enabled
          ? std::max<std::uint32_t>(1U, active_logging_config.queue_size)
          : 1U);

      auto logging_probe = std::make_shared<logging::LoggingHealthProbe>(
        std::shared_ptr<logging::ILoggingHealthSignalProvider>(logger, logger.get()));
      const auto registration_result = health_monitor->register_probe(
        HealthProbeRegistration{
          .probe_name = std::string(logging::kLoggingHealthProbeName),
          .probe_group = std::string(logging::kLoggingHealthProbeGroup),
          .probe = logging_probe.get(),
          .keepalive = logging_probe,
        });
      if (!registration_result.ok) {
      return ObservabilityLiveCompositionResult{
        .logger = nullptr,
        .audit_logger = nullptr,
        .metrics_provider = nullptr,
        .tracer_provider = nullptr,
        .health_monitor = nullptr,
        .active_logging_config = std::nullopt,
        .error = std::string("logging health probe registration failed: ") +
             (registration_result.error.has_value()
                ? registration_result.error->details.message
                : std::string("infra.health.register_probe")),
      };
      }

  return ObservabilityLiveCompositionResult{
      .logger = logger,
      .audit_logger = audit_logger,
      .metrics_provider = metrics_provider,
      .tracer_provider = tracer_provider,
        .health_monitor = health_monitor,
      .active_logging_config = active_logging_config,
      .error = {},
  };
}

}  // namespace dasall::infra