#include "observability/CognitionTelemetry.h"

#include <algorithm>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dasall::cognition::observability {
namespace {

class NoopTelemetrySink final : public ICognitionTelemetrySink {
 public:
  void emit_log(const TelemetryEvent&) override {}
  void emit_metric(const TelemetryMetric&) override {}
  void emit_trace(const TelemetryEvent&) override {}
  void emit_audit(const TelemetryEvent&) override {}
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

}  // namespace dasall::cognition::observability