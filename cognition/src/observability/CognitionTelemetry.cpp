#include "observability/CognitionTelemetry.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "audit/IAuditLogger.h"
#include "audit/AuditTypes.h"
#include "logging/ILogger.h"
#include "metrics/IMeter.h"
#include "metrics/MetricTypes.h"
#include "tracing/ISpan.h"
#include "tracing/ITracer.h"
#include "tracing/ITracerProvider.h"
#include "tracing/TraceTypes.h"

namespace dasall::cognition::observability {
namespace {

class NoopTelemetrySink final : public ICognitionTelemetrySink {
 public:
  void emit_log(const TelemetryEvent&) override {}
  void emit_metric(const TelemetryMetric&) override {}
  void emit_trace(const TelemetryEvent&) override {}
  void emit_audit(const TelemetryEvent&) override {}
};

class InfraTelemetrySink final : public ICognitionTelemetrySink {
 public:
  InfraTelemetrySink(std::shared_ptr<infra::logging::ILogger> logger,
                     std::shared_ptr<infra::audit::IAuditLogger> audit_logger,
                     std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider,
                     std::shared_ptr<infra::tracing::ITracerProvider> tracer_provider)
      : logger_(std::move(logger)),
        audit_logger_(std::move(audit_logger)),
        metrics_provider_(std::move(metrics_provider)),
        tracer_provider_(std::move(tracer_provider)) {}

  void emit_log(const TelemetryEvent& event) override {
    if (logger_ == nullptr) {
      return;
    }

    infra::LogEvent log_event{
        .level = classify_log_level(event.name),
        .module = "cognition",
        .message = std::string{"cognition "} + event.name,
        .attrs = make_log_attrs(event),
        .ts = current_time_ms(),
    };
    (void)logger_->log(log_event);
  }

  void emit_metric(const TelemetryMetric& metric) override {
    if (metrics_provider_ == nullptr) {
      return;
    }

    ensure_meter();
    if (meter_ == nullptr) {
      return;
    }

    ensure_counter(metric.name);

    infra::metrics::MetricSample sample{
        .identity_ref = infra::metrics::MetricIdentity{
            .name = metric.name,
            .type = infra::metrics::MetricType::Counter,
            .unit = "1",
            .description = "cognition telemetry event count",
        },
        .value = metric.value,
        .ts_unix_ms = current_time_ms(),
        .labels = make_metric_labels(metric),
    };
    if (!sample.is_valid()) {
      return;
    }

    (void)meter_->record(sample);
  }

  void emit_trace(const TelemetryEvent& event) override {
    if (tracer_provider_ == nullptr) {
      return;
    }

    ensure_tracer();
    if (tracer_ == nullptr) {
      return;
    }

    infra::tracing::SpanDescriptor descriptor{
        .name = std::string{"cognition."} + event.name,
        .kind = infra::tracing::SpanKind::Internal,
        .start_ts_unix_ms = current_time_ms(),
        .attrs = make_trace_attributes(event),
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
    const auto status = make_trace_status(event.name);
    span->set_status(status.first, status.second);
    (void)span->end(current_time_ms());
  }

  void emit_audit(const TelemetryEvent& event) override {
    if (audit_logger_ == nullptr) {
      return;
    }

    (void)audit_logger_->write_audit(make_audit_event(event), make_audit_context(event));
  }

 private:
  [[nodiscard]] static bool is_failure_event(const std::string_view event_name) {
    return event_name.find("failed") != std::string_view::npos;
  }

  [[nodiscard]] static bool is_degraded_event(const std::string_view event_name) {
    return event_name.find("degraded") != std::string_view::npos;
  }

  [[nodiscard]] static std::int64_t current_time_ms() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  }

  [[nodiscard]] static infra::LogLevel classify_log_level(
      const std::string_view event_name) {
    if (is_failure_event(event_name)) {
      return infra::LogLevel::Error;
    }
    if (is_degraded_event(event_name)) {
      return infra::LogLevel::Warn;
    }
    return infra::LogLevel::Info;
  }

  [[nodiscard]] static bool should_emit_log_attr(const std::string_view key) {
    static constexpr std::string_view kAllowedKeys[] = {
        "request_id",
        "goal_id",
        "profile_id",
        "stage",
        "trace_id",
        "model_hint_tier",
        "fallback_used",
        "result_code",
        "structured_projection_enabled",
        "structured_projection_required",
        "structured_schema_version",
        "structured_projection_source",
        "structured_projection_failure_code",
        "projected_node_count",
        "projected_candidate_count",
        "decision_kind",
        "confidence",
        "selected_node_id",
        "clarification_needed",
        "error_code",
        "error_stage",
        "retryable",
        "safe_to_replan",
        "fallback_mode",
        "degrade_reason",
        "omitted_details",
    };

    return std::find(std::begin(kAllowedKeys), std::end(kAllowedKeys), key) !=
           std::end(kAllowedKeys);
  }

  [[nodiscard]] static bool has_audit_refs(const AuditReferenceSet& audit_refs) {
    return !audit_refs.evidence_refs.empty() || !audit_refs.artifact_refs.empty() ||
           audit_refs.source_ref.has_value();
  }

  [[nodiscard]] static infra::LogEvent::AttributeMap make_log_attrs(
      const TelemetryEvent& event) {
    infra::LogEvent::AttributeMap attrs;
    attrs.emplace("event_name", event.name);
    for (const auto& field : event.fields) {
      if (!field.key.empty() && should_emit_log_attr(field.key)) {
        attrs[field.key] = field.value;
      }
    }

    if (has_audit_refs(event.audit_refs)) {
      attrs["audit_ref_pending"] = "true";
      attrs["audit_trace_id"] = event.context.trace_id.empty() ? "unknown"
                                                                 : event.context.trace_id;
      attrs["audit_task_id"] = event.context.stage.empty() ? "unknown"
                                                             : event.context.stage;
      if (!event.audit_refs.evidence_refs.empty()) {
        attrs["evidence_ref"] = event.audit_refs.evidence_refs.front();
        attrs["evidence_kind"] = "worker_task";
      } else if (event.audit_refs.source_ref.has_value() &&
                 !event.audit_refs.source_ref->empty()) {
        attrs["evidence_ref"] = *event.audit_refs.source_ref;
        attrs["evidence_kind"] = "source_ref";
      }
    }

    return attrs;
  }

  [[nodiscard]] static std::string fallback_unknown(const std::string& value,
                                                    const std::string_view fallback) {
    return value.empty() ? std::string(fallback) : value;
  }

  [[nodiscard]] static std::string build_detail_ref(const TelemetryEvent& event) {
    const std::string request_id = event.context.request_id.empty()
                                       ? std::string("unknown")
                                       : event.context.request_id;
    const std::string stage = event.context.stage.empty()
                                  ? std::string("unknown")
                                  : event.context.stage;
    return std::string{"telemetry://cognition/"} + event.name + "/" + request_id + "/" +
           stage;
  }

  [[nodiscard]] static infra::AuditOutcome make_audit_outcome(const std::string& event_name) {
    if (event_name == "stage.failed") {
      return infra::AuditOutcome::Failed;
    }
    if (event_name == "response.degraded") {
      return infra::AuditOutcome::Escalated;
    }
    return infra::AuditOutcome::Succeeded;
  }

  [[nodiscard]] static std::string make_metric_outcome(const std::string& metric_name) {
    if (metric_name.find("stage_failed") != std::string::npos) {
      return "failure";
    }
    if (metric_name.find("response_degraded") != std::string::npos) {
      return "degraded";
    }
    return "success";
  }

  [[nodiscard]] static std::pair<infra::tracing::SpanStatusCode, std::string>
  make_trace_status(const std::string& event_name) {
    if (event_name == "stage.failed") {
      return {infra::tracing::SpanStatusCode::Error, "cognition stage failed"};
    }
    if (event_name == "response.degraded") {
      return {infra::tracing::SpanStatusCode::Error, "cognition response degraded"};
    }
    return {infra::tracing::SpanStatusCode::Ok, std::string{}};
  }

  [[nodiscard]] static std::vector<std::string> make_audit_side_effects(
      const TelemetryEvent& event) {
    std::vector<std::string> side_effects;
    side_effects.reserve(event.fields.size() + 1U);
    side_effects.push_back(std::string{"telemetry:"} + event.name);
    for (const auto& field : event.fields) {
      const std::string side_effect = std::string{"field:"} + field.key + "=" + field.value;
      if (std::find(side_effects.begin(), side_effects.end(), side_effect) == side_effects.end()) {
        side_effects.push_back(side_effect);
      }
    }
    return side_effects;
  }

  [[nodiscard]] static infra::AuditEvent make_audit_event(const TelemetryEvent& event) {
    return infra::AuditEvent{
        .event_id = build_detail_ref(event),
        .action = std::string{"cognition."} + event.name,
        .actor = "cognition",
        .target = event.context.stage.empty() ? std::string("cognition")
                                              : std::string{"cognition."} + event.context.stage,
        .outcome = make_audit_outcome(event.name),
        .evidence_ref = infra::AuditEvidenceRef{
            .kind = infra::AuditEvidenceKind::WorkerTask,
            .ref = build_detail_ref(event),
        },
        .side_effects = make_audit_side_effects(event),
        .timestamp = current_time_ms(),
    };
  }

  [[nodiscard]] static infra::AuditContext make_audit_context(const TelemetryEvent& event) {
    return infra::AuditContext{
        .request_id = fallback_unknown(event.context.request_id, infra::kAuditContextUnknown),
        .session_id = std::string(infra::kAuditContextUnknown),
        .trace_id = fallback_unknown(event.context.trace_id, infra::kAuditContextUnknown),
        .task_id = fallback_unknown(event.context.stage, infra::kAuditContextUnknown),
        .parent_task_id = std::string(infra::kAuditContextUnknown),
        .lease_id = std::string(infra::kAuditContextUnknown),
        .worker_type = "cognition.telemetry",
    };
  }

  [[nodiscard]] static infra::metrics::MetricLabels make_metric_labels(
      const TelemetryMetric& metric) {
    std::string stage = "unknown";
    std::string profile = "unknown";
    std::string error_code;
    for (const auto& label : metric.labels) {
      if (label.key == "stage" && !label.value.empty()) {
        stage = label.value;
      } else if (label.key == "profile_id" && !label.value.empty()) {
        profile = label.value;
      } else if (label.key == "error_code") {
        error_code = label.value;
      }
    }

    return infra::metrics::MetricLabels{
        .module = "cognition",
        .stage = std::move(stage),
        .profile = std::move(profile),
        .outcome = make_metric_outcome(metric.name),
        .error_code = std::move(error_code),
    };
  }

  [[nodiscard]] static infra::tracing::TraceAttributeMap make_trace_attributes(
      const TelemetryEvent& event) {
    infra::tracing::TraceAttributeMap attrs;
    for (const auto& field : event.fields) {
      if (infra::tracing::is_valid_trace_attr_key(field.key) &&
          infra::tracing::is_printable_ascii(field.value)) {
        attrs.emplace(field.key, field.value);
      }
    }
    attrs.emplace("event_name", std::string{"cognition."} + event.name);
    return attrs;
  }

  void ensure_meter() {
    if (meter_ != nullptr || metrics_provider_ == nullptr) {
      return;
    }

    meter_ = metrics_provider_->get_meter(infra::metrics::MeterScope{
        .name = "cognition",
        .version = "v1",
        .schema_url = {},
    });
  }

  void ensure_counter(const std::string& metric_name) {
    if (meter_ == nullptr || counters_.find(metric_name) != counters_.end()) {
      return;
    }

    const auto handle = meter_->create_counter(infra::metrics::MetricIdentity{
        .name = metric_name,
        .type = infra::metrics::MetricType::Counter,
        .unit = "1",
        .description = "cognition telemetry event count",
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
        .name = "cognition",
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

[[nodiscard]] std::string bool_to_string(const bool value) {
  return value ? "true" : "false";
}

[[nodiscard]] std::string format_score(const float value) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(2) << value;
  return stream.str();
}

[[nodiscard]] std::string join_strings(const std::vector<std::string>& values,
                                       const std::string_view separator) {
  std::string joined;
  for (std::size_t index = 0U; index < values.size(); ++index) {
    if (index > 0U) {
      joined += std::string(separator);
    }
    joined += values[index];
  }
  return joined;
}

void append_field(std::vector<TelemetryField>& fields,
                  std::string key,
                  const std::optional<std::string>& value) {
  if (!value.has_value() || value->empty()) {
    return;
  }

  fields.push_back(TelemetryField{
      .key = std::move(key),
      .value = *value,
  });
}

void append_field(std::vector<TelemetryField>& fields,
                  std::string key,
                  const std::string& value) {
  if (value.empty()) {
    return;
  }

  fields.push_back(TelemetryField{
      .key = std::move(key),
      .value = value,
  });
}

[[nodiscard]] std::string decision_kind_to_string(const decision::ActionDecisionKind decision_kind) {
  switch (decision_kind) {
    case decision::ActionDecisionKind::ExecuteAction:
      return "ExecuteAction";
    case decision::ActionDecisionKind::DirectResponse:
      return "DirectResponse";
    case decision::ActionDecisionKind::AskClarification:
      return "AskClarification";
    case decision::ActionDecisionKind::ConvergeSafe:
      return "ConvergeSafe";
    case decision::ActionDecisionKind::NoDecision:
      return "NoDecision";
  }

  return "UnknownDecision";
}

[[nodiscard]] std::vector<TelemetryField> make_context_fields(
    const StageTelemetryContext& context) {
  std::vector<TelemetryField> fields;
  append_field(fields, "request_id", context.request_id);
  append_field(fields, "goal_id", context.goal_id);
  append_field(fields, "profile_id", context.profile_id);
  append_field(fields, "stage", context.stage);
  append_field(fields, "trace_id", context.trace_id);
  append_field(fields, "model_hint_tier", context.model_hint_tier);
  append_field(fields, "fallback_used", bool_to_string(context.fallback_used));
  if (context.result_code.has_value()) {
    append_field(fields, "result_code", std::to_string(*context.result_code));
  }
  append_field(fields,
               "structured_projection_enabled",
               bool_to_string(context.structured_projection.enabled));
  append_field(fields,
               "structured_projection_required",
               bool_to_string(context.structured_projection.required));
  append_field(fields,
               "structured_schema_version",
               context.structured_projection.schema_version);
  append_field(fields,
               "structured_projection_source",
               context.structured_projection.source);
  append_field(fields,
               "structured_projection_failure_code",
               context.structured_projection.failure_code);
  if (context.structured_projection.projected_node_count.has_value()) {
    append_field(fields,
                 "projected_node_count",
                 std::to_string(*context.structured_projection.projected_node_count));
  }
  if (context.structured_projection.projected_candidate_count.has_value()) {
    append_field(fields,
                 "projected_candidate_count",
                 std::to_string(*context.structured_projection.projected_candidate_count));
  }
  return fields;
}

[[nodiscard]] std::string summarize_candidate_scores(
    const std::vector<decision::CandidateDecisionScore>& candidate_scores) {
  std::vector<std::string> formatted_scores;
  formatted_scores.reserve(candidate_scores.size());
  for (const auto& score : candidate_scores) {
    formatted_scores.push_back(score.candidate_name + "=" + format_score(score.score));
  }
  return join_strings(formatted_scores, ",");
}

[[nodiscard]] std::string build_metric_name(const std::string_view event_name) {
  std::string metric_name{"cognition_"};
  for (const char ch : event_name) {
    metric_name += ch == '.' ? '_' : ch;
  }
  metric_name += "_total";
  return metric_name;
}

[[nodiscard]] bool replace_json_assignment(std::string& text,
                                           const std::string_view& key,
                                           std::vector<std::string>& omitted_details) {
  const std::string pattern = "\"" + std::string(key) + "\":\"";
  bool replaced = false;
  std::size_t position = 0U;
  while ((position = text.find(pattern, position)) != std::string::npos) {
    const auto value_begin = position + pattern.size();
    const auto value_end = text.find('"', value_begin);
    if (value_end == std::string::npos) {
      break;
    }
    text.replace(value_begin, value_end - value_begin, "[REDACTED]");
    omitted_details.push_back(std::string("redacted:") + std::string(key));
    position = value_begin + std::string_view{"[REDACTED]"}.size();
    replaced = true;
  }
  return replaced;
}

[[nodiscard]] bool replace_plain_assignment(std::string& text,
                                            const std::string_view& key,
                                            std::vector<std::string>& omitted_details) {
  const std::string pattern = std::string(key) + "=";
  bool replaced = false;
  std::size_t position = 0U;
  while ((position = text.find(pattern, position)) != std::string::npos) {
    const auto value_begin = position + pattern.size();
    const auto value_end = text.find_first_of(",; \n\r\t}", value_begin);
    const auto effective_end = value_end == std::string::npos ? text.size() : value_end;
    text.replace(value_begin, effective_end - value_begin, "[REDACTED]");
    omitted_details.push_back(std::string("redacted:") + std::string(key));
    position = value_begin + std::string_view{"[REDACTED]"}.size();
    replaced = true;
  }
  return replaced;
}

struct RedactionOutcome {
  std::string value;
  std::vector<std::string> omitted_details;
  bool redacted = false;
};

[[nodiscard]] RedactionOutcome redact_value(const CognitionConfig& config,
                                            std::string value) {
  RedactionOutcome outcome{
      .value = std::move(value),
      .omitted_details = {},
      .redacted = false,
  };
  if (!config.observability.redact_context_payload) {
    return outcome;
  }

  static constexpr std::string_view kSensitiveKeys[] = {
      "reasoning_content",
      "raw_prompt",
      "prompt_bundle",
      "provider_payload",
      "reasoning_trace",
      "api_token",
      "authorization",
      "secret_key",
  };

  try {
    for (const auto& key : kSensitiveKeys) {
      const auto json_replaced = replace_json_assignment(outcome.value, key, outcome.omitted_details);
      const auto plain_replaced =
          replace_plain_assignment(outcome.value, key, outcome.omitted_details);
      outcome.redacted = outcome.redacted || json_replaced || plain_replaced;
    }
  } catch (...) {
    outcome.value = "[REDACTED]";
    outcome.omitted_details = {"redaction_failed"};
    outcome.redacted = true;
  }

  return outcome;
}

[[nodiscard]] TelemetryEvent redact_event_fields(const CognitionConfig& config,
                                                 TelemetryEvent event,
                                                 TelemetryEmitResult& emit_result) {
  std::vector<std::string> redaction_notes;
  for (auto& field : event.fields) {
    const auto redaction = redact_value(config, field.value);
    field.value = redaction.value;
    if (redaction.redacted) {
      emit_result.redacted = true;
      redaction_notes.insert(redaction_notes.end(),
                             redaction.omitted_details.begin(),
                             redaction.omitted_details.end());
    }
  }

  if (!redaction_notes.empty()) {
    append_field(event.fields, "omitted_details", join_strings(redaction_notes, ","));
  }

  return event;
}

void append_unique(std::vector<std::string>& values, const std::vector<std::string>& extra_values) {
  for (const auto& value : extra_values) {
    if (std::find(values.begin(), values.end(), value) == values.end()) {
      values.push_back(value);
    }
  }
}

}  // namespace

CognitionTelemetry::CognitionTelemetry(CognitionConfig config,
                                       std::shared_ptr<ICognitionTelemetrySink> sink)
    : config_(std::move(config)),
      sink_(sink == nullptr ? std::make_shared<NoopTelemetrySink>() : std::move(sink)) {}

TelemetryEmitResult CognitionTelemetry::emit_stage_started(
    const StageTelemetryContext& context) const {
  TelemetryEvent event{
      .name = "stage.started",
      .context = context,
      .fields = make_context_fields(context),
      .audit_refs = {},
  };
  TelemetryMetric metric{
      .name = build_metric_name(event.name),
      .value = 1.0,
      .labels = make_context_fields(context),
  };
  return emit_event(std::move(event), std::move(metric));
}

TelemetryEmitResult CognitionTelemetry::emit_stage_completed(
    const StageTelemetryContext& context,
    const DecisionTelemetryRecord& record) const {
  auto fields = make_context_fields(context);
  append_field(fields, "decision_kind", decision_kind_to_string(record.decision_kind));
  append_field(fields, "confidence", format_score(record.confidence));
  append_field(fields, "candidate_scores", summarize_candidate_scores(record.candidate_scores));
  append_field(fields, "selected_node_id", record.selected_node_id);
  append_field(fields, "clarification_needed", bool_to_string(record.clarification_needed));
  append_field(fields, "clarification_question", record.clarification_question);
  append_field(fields, "response_summary", record.response_summary);

  TelemetryEvent event{
      .name = "stage.completed",
      .context = context,
      .fields = std::move(fields),
      .audit_refs = record.audit_refs,
  };
  auto metric_labels = make_context_fields(context);
  append_field(metric_labels, "decision_kind", decision_kind_to_string(record.decision_kind));
  TelemetryMetric metric{
      .name = build_metric_name(event.name),
      .value = 1.0,
      .labels = std::move(metric_labels),
  };
  return emit_event(std::move(event), std::move(metric));
}

TelemetryEmitResult CognitionTelemetry::emit_stage_failed(
    const StageTelemetryContext& context,
    const contracts::ErrorInfo& error_info) const {
  auto fields = make_context_fields(context);
  if (error_info.details.code.has_value()) {
    append_field(fields, "error_code", std::to_string(*error_info.details.code));
  }
  append_field(fields, "error_stage", error_info.details.stage);
  append_field(fields, "error_message", error_info.details.message);
  if (error_info.retryable.has_value()) {
    append_field(fields, "retryable", bool_to_string(*error_info.retryable));
  }
  if (error_info.safe_to_replan.has_value()) {
    append_field(fields, "safe_to_replan", bool_to_string(*error_info.safe_to_replan));
  }

  TelemetryEvent event{
      .name = "stage.failed",
      .context = context,
      .fields = std::move(fields),
      .audit_refs = AuditReferenceSet{
          .evidence_refs = {},
          .artifact_refs = {},
          .source_ref = error_info.source_ref.ref_id,
      },
  };
  auto metric_labels = make_context_fields(context);
  if (error_info.details.code.has_value()) {
    append_field(metric_labels, "error_code", std::to_string(*error_info.details.code));
  }
  TelemetryMetric metric{
      .name = build_metric_name(event.name),
      .value = 1.0,
      .labels = std::move(metric_labels),
  };
  return emit_event(std::move(event), std::move(metric));
}

TelemetryEmitResult CognitionTelemetry::emit_clarification_requested(
    const StageTelemetryContext& context,
    const DecisionTelemetryRecord& record) const {
  auto fields = make_context_fields(context);
  append_field(fields, "decision_kind", decision_kind_to_string(record.decision_kind));
  append_field(fields, "confidence", format_score(record.confidence));
  append_field(fields, "clarification_question", record.clarification_question);

  TelemetryEvent event{
      .name = "clarification.requested",
      .context = context,
      .fields = std::move(fields),
      .audit_refs = record.audit_refs,
  };
  TelemetryMetric metric{
      .name = build_metric_name(event.name),
      .value = 1.0,
      .labels = make_context_fields(context),
  };
  return emit_event(std::move(event), std::move(metric));
}

TelemetryEmitResult CognitionTelemetry::emit_response_degraded(
    const StageTelemetryContext& context,
    const DegradeTelemetryRecord& record) const {
  auto fields = make_context_fields(context);
  append_field(fields, "fallback_mode", record.fallback_mode);
  append_field(fields, "degrade_reason", record.reason);
  append_field(fields, "payload_excerpt", record.payload_excerpt);
  append_field(fields, "omitted_details", join_strings(record.omitted_details, ","));

  TelemetryEvent event{
      .name = "response.degraded",
      .context = context,
      .fields = std::move(fields),
      .audit_refs = record.audit_refs,
  };
  auto metric_labels = make_context_fields(context);
  append_field(metric_labels, "fallback_mode", record.fallback_mode);
  TelemetryMetric metric{
      .name = build_metric_name(event.name),
      .value = 1.0,
      .labels = std::move(metric_labels),
  };
  return emit_event(std::move(event), std::move(metric));
}

DecisionTelemetryRecord CognitionTelemetry::make_decision_record(
    const decision::ActionDecision& action_decision) {
  DecisionTelemetryRecord record;
  record.decision_kind = action_decision.decision_kind;
  record.confidence = action_decision.confidence;
  record.candidate_scores = action_decision.candidate_scores;
  record.selected_node_id = action_decision.selected_node_id;
  record.clarification_needed = action_decision.clarification_needed;
  record.clarification_question = action_decision.clarification_question;
  if (action_decision.response_outline.has_value()) {
    record.response_summary = action_decision.response_outline->summary;
  }
  if (action_decision.tool_intent_hint.has_value()) {
    append_unique(record.audit_refs.evidence_refs, action_decision.tool_intent_hint->evidence_refs);
  }
  return record;
}

TelemetryEmitResult CognitionTelemetry::emit_event(TelemetryEvent event,
                                                   TelemetryMetric metric) const {
  TelemetryEmitResult result;
  event = redact_event_fields(config_, std::move(event), result);

  try {
    sink_->emit_log(event);
    result.emitted = true;
  } catch (...) {
    result.diagnostics.push_back("telemetry_sink_failure:log");
  }

  try {
    sink_->emit_metric(metric);
    result.emitted = true;
  } catch (...) {
    result.diagnostics.push_back("telemetry_sink_failure:metric");
  }

  if (config_.observability.emit_stage_spans) {
    try {
      sink_->emit_trace(event);
      result.emitted = true;
    } catch (...) {
      result.diagnostics.push_back("telemetry_sink_failure:trace");
    }
  }

  try {
    sink_->emit_audit(event);
    result.emitted = true;
  } catch (...) {
    result.diagnostics.push_back("telemetry_sink_failure:audit");
  }

  return result;
}

std::shared_ptr<ICognitionTelemetrySink> make_live_telemetry_sink(
    const CognitionRuntimeDependencies& dependencies) {
  if (dependencies.logger == nullptr && dependencies.audit_logger == nullptr &&
      dependencies.metrics_provider == nullptr &&
      dependencies.tracer_provider == nullptr) {
    return nullptr;
  }

  return std::make_shared<InfraTelemetrySink>(dependencies.logger,
                                              dependencies.audit_logger,
                                              dependencies.metrics_provider,
                                              dependencies.tracer_provider);
}

}  // namespace dasall::cognition::observability