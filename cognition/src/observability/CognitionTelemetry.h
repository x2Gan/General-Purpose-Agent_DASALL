#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "CognitionConfig.h"
#include "CognitionDependencies.h"
#include "decision/ActionDecision.h"
#include "error/ErrorInfo.h"
#include "metrics/MetricTypes.h"

namespace dasall::cognition::observability {

struct TelemetryField {
  std::string key;
  std::string value;
};

struct StructuredProjectionTelemetry {
  bool enabled = false;
  bool required = false;
  std::optional<std::string> schema_version;
  std::optional<std::string> source;
  std::optional<std::string> failure_code;
  std::optional<std::uint32_t> projected_node_count;
  std::optional<std::uint32_t> projected_candidate_count;
};

struct StageTelemetryContext {
  std::string request_id;
  std::string goal_id;
  std::string profile_id;
  std::string stage;
  std::string trace_id;
  std::string model_hint_tier;
  bool fallback_used = false;
  std::optional<int> result_code;
  StructuredProjectionTelemetry structured_projection;
  std::optional<std::uint32_t> latency_ms;
};

struct AuditReferenceSet {
  std::vector<std::string> evidence_refs;
  std::vector<std::string> artifact_refs;
  std::optional<std::string> source_ref;
};

struct DecisionTelemetryRecord {
  decision::ActionDecisionKind decision_kind = decision::ActionDecisionKind::NoDecision;
  float confidence = 0.0F;
  std::vector<decision::CandidateDecisionScore> candidate_scores;
  std::optional<std::string> selected_node_id;
  bool clarification_needed = false;
  std::optional<std::string> clarification_question;
  std::optional<std::string> response_summary;
  AuditReferenceSet audit_refs;
};

struct DegradeTelemetryRecord {
  std::string fallback_mode;
  std::string reason;
  std::optional<std::string> resolved_route;
  std::optional<std::string> failure_category;
  std::optional<std::string> error_type;
  std::optional<std::string> payload_excerpt;
  std::vector<std::string> omitted_details;
  AuditReferenceSet audit_refs;
};

struct TelemetryEvent {
  std::string name;
  StageTelemetryContext context;
  std::vector<TelemetryField> fields;
  AuditReferenceSet audit_refs;
};

struct TelemetryMetric {
  std::string name;
  double value = 0.0;
  std::vector<TelemetryField> labels;
  infra::metrics::MetricType type = infra::metrics::MetricType::Counter;
  std::string unit = "1";
  std::string description;
};

struct TelemetryEmitResult {
  bool emitted = false;
  bool redacted = false;
  std::vector<std::string> diagnostics;
};

class ICognitionTelemetrySink {
 public:
  virtual ~ICognitionTelemetrySink() = default;

  virtual void emit_log(const TelemetryEvent& event) = 0;
  virtual void emit_metric(const TelemetryMetric& metric) = 0;
  virtual void emit_trace(const TelemetryEvent& event) = 0;
  virtual void emit_audit(const TelemetryEvent& event) = 0;
};

class CognitionTelemetry {
 public:
  explicit CognitionTelemetry(
      CognitionConfig config = {},
      std::shared_ptr<ICognitionTelemetrySink> sink = nullptr);

  [[nodiscard]] TelemetryEmitResult emit_stage_started(
      const StageTelemetryContext& context) const;
  [[nodiscard]] TelemetryEmitResult emit_stage_completed(
      const StageTelemetryContext& context,
      const DecisionTelemetryRecord& record) const;
  [[nodiscard]] TelemetryEmitResult emit_stage_failed(
      const StageTelemetryContext& context,
      const contracts::ErrorInfo& error_info) const;
  [[nodiscard]] TelemetryEmitResult emit_clarification_requested(
      const StageTelemetryContext& context,
      const DecisionTelemetryRecord& record) const;
  [[nodiscard]] TelemetryEmitResult emit_response_degraded(
      const StageTelemetryContext& context,
      const DegradeTelemetryRecord& record) const;
    [[nodiscard]] TelemetryEmitResult emit_detail_event(
      std::string event_name,
      const StageTelemetryContext& context,
      std::vector<TelemetryField> fields = {},
      AuditReferenceSet audit_refs = {}) const;

  [[nodiscard]] static DecisionTelemetryRecord make_decision_record(
      const decision::ActionDecision& action_decision);

 private:
  [[nodiscard]] TelemetryEmitResult emit_event(
      TelemetryEvent event,
      std::vector<TelemetryMetric> metrics) const;

  CognitionConfig config_;
  std::shared_ptr<ICognitionTelemetrySink> sink_;
};

[[nodiscard]] std::shared_ptr<ICognitionTelemetrySink> make_live_telemetry_sink(
    const CognitionRuntimeDependencies& dependencies);

}  // namespace dasall::cognition::observability