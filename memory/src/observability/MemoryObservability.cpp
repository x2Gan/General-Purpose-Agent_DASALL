#include "observability/MemoryObservability.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "LogEvent.h"
#include "audit/AuditTypes.h"
#include "audit/IAuditLogger.h"
#include "logging/ILogger.h"
#include "metrics/IMeter.h"
#include "metrics/IMetricsProvider.h"
#include "metrics/MetricTypes.h"
#include "tracing/ISpan.h"
#include "tracing/ITracer.h"
#include "tracing/ITracerProvider.h"
#include "tracing/TraceTypes.h"

namespace dasall::memory::observability {
namespace {

[[nodiscard]] std::int64_t current_time_ms() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

[[nodiscard]] bool is_failure_event(const std::string_view event_name) {
  return event_name.find("failed") != std::string_view::npos;
}

[[nodiscard]] bool is_degraded_event(const std::string_view event_name) {
  return event_name.find("degraded") != std::string_view::npos;
}

[[nodiscard]] std::string fallback_unknown(
    const std::string& value,
    const std::string_view fallback = "unknown") {
  return value.empty() ? std::string(fallback) : value;
}

[[nodiscard]] std::string sanitize_metric_name(const std::string_view event_name) {
  std::string metric_name = "memory_";
  metric_name.reserve(metric_name.size() + event_name.size() + 6U);
  for (const char ch : event_name) {
    metric_name.push_back(ch == '.' ? '_' : ch);
  }
  metric_name += "_total";
  return metric_name;
}

[[nodiscard]] std::string make_detail_ref(
    const std::string_view event_name,
    const MemoryTelemetryContext& context) {
  return std::string{"telemetry://memory/"} + std::string(event_name) + "/" +
         fallback_unknown(context.request_id) + "/" +
         fallback_unknown(context.stage);
}

[[nodiscard]] infra::LogLevel classify_log_level(const std::string_view event_name) {
  if (is_failure_event(event_name)) {
    return infra::LogLevel::Error;
  }
  if (is_degraded_event(event_name)) {
    return infra::LogLevel::Warn;
  }
  return infra::LogLevel::Info;
}

[[nodiscard]] infra::AuditOutcome classify_audit_outcome(
    const std::string_view event_name) {
  if (is_failure_event(event_name)) {
    return infra::AuditOutcome::Failed;
  }
  if (is_degraded_event(event_name)) {
    return infra::AuditOutcome::Escalated;
  }
  return infra::AuditOutcome::Succeeded;
}

[[nodiscard]] std::string classify_metric_outcome(const std::string_view event_name) {
  if (is_failure_event(event_name)) {
    return "failure";
  }
  if (is_degraded_event(event_name)) {
    return "degraded";
  }
  return "success";
}

[[nodiscard]] std::pair<infra::tracing::SpanStatusCode, std::string>
classify_span_status(const std::string_view event_name) {
  if (is_failure_event(event_name)) {
    return {infra::tracing::SpanStatusCode::Error,
            "memory event failed"};
  }
  if (is_degraded_event(event_name)) {
    return {infra::tracing::SpanStatusCode::Error,
            "memory event degraded"};
  }
  return {infra::tracing::SpanStatusCode::Ok, std::string{}};
}

[[nodiscard]] std::optional<std::string> find_field(
    const std::vector<MemoryTelemetryField>& fields,
    const std::string_view key) {
  const auto it = std::find_if(fields.begin(), fields.end(),
                               [&key](const MemoryTelemetryField& field) {
                                 return field.key == key;
                               });
  if (it == fields.end() || it->value.empty()) {
    return std::nullopt;
  }
  return it->value;
}

[[nodiscard]] bool should_emit_log_attr(const std::string_view key) {
  static constexpr std::string_view kAllowedKeys[] = {
      "warning_count",
      "warning",
      "warning_codes",
      "dropped_section_count",
      "compression_note_count",
      "degraded",
      "result_code",
      "failure_reason",
      "fact_count",
      "experience_count",
      "conflict_count",
      "partial",
      "retryable",
      "storage_backend",
      "vector_enabled",
      "auto_schedule",
      "lifecycle_state",
      "turn_id",
      "summary_id",
      "new_fact_id",
      "existing_fact_id",
      "reason",
      "confidence_delta",
      "checkpoint_requested",
      "retention_requested",
      "quarantine_requested",
      "vector_rebuild_requested",
      "checkpoint_executed",
      "turns_purged",
      "facts_purged",
      "experiences_purged",
      "quarantine_cleaned",
      "duration_ms",
  };

  return std::find(std::begin(kAllowedKeys), std::end(kAllowedKeys), key) !=
         std::end(kAllowedKeys);
}

[[nodiscard]] infra::LogEvent::AttributeMap make_log_attrs(
    const std::string_view event_name,
    const MemoryTelemetryContext& context,
    const std::vector<MemoryTelemetryField>& fields) {
  infra::LogEvent::AttributeMap attrs;
  attrs.emplace("event_name", std::string(event_name));
  attrs.emplace("request_id", fallback_unknown(context.request_id));
  attrs.emplace("session_id", fallback_unknown(context.session_id));
  attrs.emplace("trace_id", fallback_unknown(context.trace_id));
  attrs.emplace("stage", fallback_unknown(context.stage));
  attrs.emplace("profile_id", fallback_unknown(context.profile_id));
  for (const auto& field : fields) {
    if (!field.key.empty() && should_emit_log_attr(field.key)) {
      attrs[field.key] = field.value;
    }
  }
  return attrs;
}

[[nodiscard]] infra::metrics::MetricLabels make_metric_labels(
    const std::string_view event_name,
    const MemoryTelemetryContext& context,
    const std::vector<MemoryTelemetryField>& fields) {
  std::string error_code;
  if (const auto result_code = find_field(fields, "result_code");
      result_code.has_value()) {
    error_code = *result_code;
  } else if (const auto warning = find_field(fields, "warning");
             warning.has_value() && classify_metric_outcome(event_name) != "success") {
    error_code = *warning;
  }

  return infra::metrics::MetricLabels{
      .module = "memory",
      .stage = fallback_unknown(context.stage),
      .profile = fallback_unknown(context.profile_id),
      .outcome = classify_metric_outcome(event_name),
      .error_code = std::move(error_code),
  };
}

[[nodiscard]] infra::tracing::TraceAttributeMap make_trace_attrs(
    const std::string_view event_name,
    const MemoryTelemetryContext& context,
    const std::vector<MemoryTelemetryField>& fields) {
  infra::tracing::TraceAttributeMap attrs;
  attrs.emplace("event_name", std::string("memory.") + std::string(event_name));
  attrs.emplace("request_id", fallback_unknown(context.request_id));
  attrs.emplace("session_id", fallback_unknown(context.session_id));
  attrs.emplace("stage", fallback_unknown(context.stage));
  attrs.emplace("profile_id", fallback_unknown(context.profile_id));
  if (!context.trace_id.empty()) {
    attrs.emplace("trace_id", context.trace_id);
  }
  for (const auto& field : fields) {
    if (!infra::tracing::is_valid_trace_attr_key(field.key) ||
        !infra::tracing::is_printable_ascii(field.value)) {
      continue;
    }
    attrs.emplace(field.key, field.value);
  }
  return attrs;
}

[[nodiscard]] std::vector<std::string> make_audit_side_effects(
    const std::string_view event_name,
    const std::vector<MemoryTelemetryField>& fields) {
  std::vector<std::string> side_effects;
  side_effects.push_back(std::string("event:") + std::string(event_name));
  for (const auto& field : fields) {
    const std::string entry = std::string("field:") + field.key + "=" + field.value;
    if (!field.key.empty() &&
        std::find(side_effects.begin(), side_effects.end(), entry) == side_effects.end()) {
      side_effects.push_back(entry);
    }
  }
  return side_effects;
}

class NoopTelemetrySink final : public IMemoryTelemetrySink {
 public:
  void emit_log(const std::string&,
                const MemoryTelemetryContext&,
                const std::vector<MemoryTelemetryField>&) override {}
  void emit_metric(const std::string&,
                   const MemoryTelemetryContext&,
                   const std::vector<MemoryTelemetryField>&) override {}
  void emit_trace(const std::string&,
                  const MemoryTelemetryContext&,
                  const std::vector<MemoryTelemetryField>&) override {}
  void emit_audit(const std::string&,
                  const MemoryTelemetryContext&,
                  const std::vector<MemoryTelemetryField>&) override {}
};

class InfraTelemetrySink final : public IMemoryTelemetrySink {
 public:
  InfraTelemetrySink(
      std::shared_ptr<infra::logging::ILogger> logger,
      std::shared_ptr<infra::audit::IAuditLogger> audit_logger,
      std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider,
      std::shared_ptr<infra::tracing::ITracerProvider> tracer_provider)
      : logger_(std::move(logger)),
        audit_logger_(std::move(audit_logger)),
        metrics_provider_(std::move(metrics_provider)),
        tracer_provider_(std::move(tracer_provider)) {}

  void emit_log(const std::string& event_name,
                const MemoryTelemetryContext& context,
                const std::vector<MemoryTelemetryField>& fields) override {
    if (logger_ == nullptr) {
      return;
    }

    infra::LogEvent event{
        .level = classify_log_level(event_name),
        .module = "memory",
        .message = std::string("memory ") + event_name,
        .attrs = make_log_attrs(event_name, context, fields),
        .ts = current_time_ms(),
    };
    (void)logger_->log(event);
  }

  void emit_metric(const std::string& event_name,
                   const MemoryTelemetryContext& context,
                   const std::vector<MemoryTelemetryField>& fields) override {
    if (metrics_provider_ == nullptr) {
      return;
    }

    ensure_meter();
    if (meter_ == nullptr) {
      return;
    }

    const auto metric_name = sanitize_metric_name(event_name);
    ensure_counter(metric_name);
    infra::metrics::MetricSample sample{
        .identity_ref = infra::metrics::MetricIdentity{
            .name = metric_name,
            .type = infra::metrics::MetricType::Counter,
            .unit = "1",
            .description = "memory telemetry event count",
        },
        .value = 1.0,
        .ts_unix_ms = current_time_ms(),
        .labels = make_metric_labels(event_name, context, fields),
    };
    if (!sample.is_valid()) {
      return;
    }

    (void)meter_->record(sample);
  }

  void emit_trace(const std::string& event_name,
                  const MemoryTelemetryContext& context,
                  const std::vector<MemoryTelemetryField>& fields) override {
    if (tracer_provider_ == nullptr) {
      return;
    }

    ensure_tracer();
    if (tracer_ == nullptr) {
      return;
    }

    infra::tracing::SpanDescriptor descriptor{
        .name = std::string("memory.") + event_name,
        .kind = infra::tracing::SpanKind::Internal,
        .start_ts_unix_ms = current_time_ms(),
        .attrs = make_trace_attrs(event_name, context, fields),
        .links = {},
    };
    if (!descriptor.is_valid()) {
      return;
    }

    auto span = tracer_->start_span(descriptor, nullptr);
    if (span == nullptr) {
      return;
    }

    span->add_event(descriptor.name, descriptor.attrs);
    const auto status = classify_span_status(event_name);
    span->set_status(status.first, status.second);
    (void)span->end(current_time_ms());
  }

  void emit_audit(const std::string& event_name,
                  const MemoryTelemetryContext& context,
                  const std::vector<MemoryTelemetryField>& fields) override {
    if (audit_logger_ == nullptr) {
      return;
    }

    (void)audit_logger_->write_audit(
        infra::AuditEvent{
            .event_id = make_detail_ref(event_name, context),
            .action = std::string("memory.") + event_name,
            .actor = "memory",
            .target = context.stage.empty() ? std::string("memory")
                                            : std::string("memory.") + context.stage,
            .outcome = classify_audit_outcome(event_name),
            .evidence_ref = infra::AuditEvidenceRef{
                .kind = infra::AuditEvidenceKind::WorkerTask,
                .ref = make_detail_ref(event_name, context),
            },
            .side_effects = make_audit_side_effects(event_name, fields),
            .timestamp = current_time_ms(),
        },
        infra::AuditContext{
            .request_id = fallback_unknown(context.request_id, infra::kAuditContextUnknown),
            .session_id = fallback_unknown(context.session_id, infra::kAuditContextUnknown),
            .trace_id = fallback_unknown(context.trace_id, infra::kAuditContextUnknown),
            .task_id = fallback_unknown(context.stage, infra::kAuditContextUnknown),
            .parent_task_id = std::string(infra::kAuditContextUnknown),
            .lease_id = std::string(infra::kAuditContextUnknown),
            .worker_type = "memory.telemetry",
        });
  }

 private:
  void ensure_meter() {
    if (meter_ != nullptr || metrics_provider_ == nullptr) {
      return;
    }

    meter_ = metrics_provider_->get_meter(infra::metrics::MeterScope{
        .name = "memory",
        .version = "v1",
        .schema_url = {},
    });
  }

  void ensure_counter(const std::string& metric_name) {
    if (meter_ == nullptr || counters_.contains(metric_name)) {
      return;
    }

    const auto handle = meter_->create_counter(infra::metrics::MetricIdentity{
        .name = metric_name,
        .type = infra::metrics::MetricType::Counter,
        .unit = "1",
        .description = "memory telemetry event count",
    });
    if (handle.has_value()) {
      counters_.emplace(metric_name, *handle);
    }
  }

  void ensure_tracer() {
    if (tracer_ != nullptr || tracer_provider_ == nullptr) {
      return;
    }

    tracer_ = tracer_provider_->get_tracer(infra::tracing::TracerScope{
        .name = "memory",
        .version = "v1",
        .schema_url = {},
    });
  }

  std::shared_ptr<infra::logging::ILogger> logger_;
  std::shared_ptr<infra::audit::IAuditLogger> audit_logger_;
  std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider_;
  std::shared_ptr<infra::tracing::ITracerProvider> tracer_provider_;
  std::shared_ptr<infra::metrics::IMeter> meter_;
  std::shared_ptr<infra::tracing::ITracer> tracer_;
  std::map<std::string, infra::metrics::InstrumentHandle> counters_;
};

}  // namespace

MemoryObservability::MemoryObservability(
    std::shared_ptr<IMemoryTelemetrySink> sink,
    std::string default_profile_id)
    : sink_(std::move(sink)),
      default_profile_id_(default_profile_id.empty() ? "unknown"
                                                     : std::move(default_profile_id)) {
  if (sink_ == nullptr) {
    sink_ = std::make_shared<NoopTelemetrySink>();
  }
}

void MemoryObservability::emit(
    const std::string& event_name,
    const MemoryTelemetryContext& context,
    std::vector<MemoryTelemetryField> fields) const {
  if (sink_ == nullptr || event_name.empty()) {
    return;
  }

  auto enriched_context = context;
  if (enriched_context.profile_id.empty()) {
    enriched_context.profile_id = default_profile_id_;
  }

  sink_->emit_log(event_name, enriched_context, fields);
  sink_->emit_metric(event_name, enriched_context, fields);
  sink_->emit_trace(event_name, enriched_context, fields);
  sink_->emit_audit(event_name, enriched_context, fields);
}

std::shared_ptr<IMemoryTelemetrySink> make_live_telemetry_sink(
    const MemoryRuntimeDependencies& dependencies) {
  if (dependencies.logger == nullptr && dependencies.audit_logger == nullptr &&
      dependencies.metrics_provider == nullptr &&
      dependencies.tracer_provider == nullptr) {
    return std::make_shared<NoopTelemetrySink>();
  }

  return std::make_shared<InfraTelemetrySink>(
      dependencies.logger,
      dependencies.audit_logger,
      dependencies.metrics_provider,
      dependencies.tracer_provider);
}

}  // namespace dasall::memory::observability