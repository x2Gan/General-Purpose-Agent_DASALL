#include "tracing/TraceAuditBridge.h"

#include <chrono>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dasall::infra::tracing {
namespace {

constexpr std::string_view kTraceAuditBridgeSourceRef = "TraceAuditBridge";

[[nodiscard]] std::string normalized_or(std::string value,
                                        const std::string_view& fallback) {
  if (value.empty()) {
    return std::string(fallback);
  }

  return value;
}

[[nodiscard]] AuditOutcome audit_outcome_from_bridge_outcome(
    TraceAuditEventOutcome outcome) {
  switch (outcome) {
    case TraceAuditEventOutcome::Success:
      return AuditOutcome::Succeeded;
    case TraceAuditEventOutcome::Failure:
      return AuditOutcome::Failed;
    case TraceAuditEventOutcome::Degraded:
      return AuditOutcome::Escalated;
  }

  return AuditOutcome::Failed;
}

[[nodiscard]] AuditWriteOutcome make_write_failure_outcome(
    contracts::ResultCode result_code) {
  return AuditWriteOutcome{
      .accepted = false,
      .persisted = false,
      .fallback_used = false,
      .error_code = result_code,
  };
}

void append_side_effect(std::vector<std::string>* side_effects,
                        const std::string_view& key,
                        std::string value) {
  if (!value.empty()) {
    side_effects->push_back(std::string(key) + ":" + std::move(value));
  }
}

[[nodiscard]] std::string trace_audit_error_code_label(
    const TraceAuditEvent& event) {
  if (!event.error_code.has_value()) {
    return "none";
  }

  return std::string(trace_error_code_name(*event.error_code));
}

[[nodiscard]] std::string trace_audit_target(TraceAuditEventKind kind) {
  switch (kind) {
    case TraceAuditEventKind::SamplerConfigChange:
      return "tracing:sampler";
    case TraceAuditEventKind::ExportRecoveryTransition:
      return "tracing:exporter";
    case TraceAuditEventKind::ShutdownFallback:
      return "tracing:shutdown";
  }

  return "tracing:unknown";
}

}  // namespace

TraceAuditBridge::TraceAuditBridge(
    std::shared_ptr<audit::IAuditLogger> audit_logger,
    TraceAuditBridgeOptions options)
    : audit_logger_(std::move(audit_logger)),
      options_(std::move(options)),
      last_detail_ref_(normalized_or(options_.detail_ref_prefix,
                                     "status://tracing/audit/") + "idle") {
  options_.detail_ref_prefix = normalized_or(options_.detail_ref_prefix,
                                             "status://tracing/audit/");
  options_.event_id_prefix = normalized_or(options_.event_id_prefix,
                                           "trace-audit-event-");
}

void TraceAuditBridge::set_audit_logger(
    std::shared_ptr<audit::IAuditLogger> audit_logger) {
  audit_logger_ = std::move(audit_logger);
}

TraceAuditWriteResult TraceAuditBridge::write_audit_event(
    const TraceAuditEvent& event) {
  const std::string detail_ref =
      event.detail_ref.empty() ? options_.detail_ref_prefix + "invalid_event"
                               : event.detail_ref;
  if (!event.is_valid()) {
    record_failure(contracts::ResultCode::ValidationFieldMissing, detail_ref);
    return TraceAuditWriteResult{
        .emitted = false,
        .audit_event = {},
        .audit_context = {},
        .write_outcome =
            make_write_failure_outcome(contracts::ResultCode::ValidationFieldMissing),
    };
  }

  if (!audit_logger_) {
    record_failure(contracts::ResultCode::RuntimeRetryExhausted, detail_ref);
    return TraceAuditWriteResult{
        .emitted = false,
        .audit_event = {},
        .audit_context = {},
        .write_outcome =
            make_write_failure_outcome(contracts::ResultCode::RuntimeRetryExhausted),
    };
  }

  auto audit_event = make_audit_event(event);
  auto audit_context = make_audit_context(event);
  if (!audit_event.has_required_fields() ||
      !audit_event.side_effects_are_serializable() ||
      !audit_context.has_non_empty_fields()) {
    record_failure(contracts::ResultCode::ValidationFieldMissing, detail_ref);
    return TraceAuditWriteResult{
        .emitted = false,
        .audit_event = std::move(audit_event),
        .audit_context = std::move(audit_context),
        .write_outcome =
            make_write_failure_outcome(contracts::ResultCode::ValidationFieldMissing),
    };
  }

  const auto write_outcome = audit_logger_->write_audit(audit_event, audit_context);
  if (!write_outcome.is_success() && !write_outcome.is_degraded_success()) {
    record_failure(write_outcome.error_code, detail_ref);
    return TraceAuditWriteResult{
        .emitted = false,
        .audit_event = std::move(audit_event),
        .audit_context = std::move(audit_context),
        .write_outcome = write_outcome,
    };
  }

  ++emitted_total_;
  record_success(detail_ref);
  return TraceAuditWriteResult{
      .emitted = true,
      .audit_event = std::move(audit_event),
      .audit_context = std::move(audit_context),
      .write_outcome = write_outcome,
  };
}

TraceAuditBridgeStatus TraceAuditBridge::get_status() const {
  return TraceAuditBridgeStatus{
      .emitted_total = emitted_total_,
      .emit_failures = emit_failures_,
      .degraded = emit_failures_ > 0 || last_error_code_.has_value(),
      .last_error_code = last_error_code_,
      .detail_ref = last_detail_ref_.empty() ? options_.detail_ref_prefix + "idle"
                                             : last_detail_ref_,
  };
}

AuditEvent TraceAuditBridge::make_audit_event(const TraceAuditEvent& event) {
  std::vector<std::string> side_effects;
  append_side_effect(&side_effects, "stage", event.stage);
  append_side_effect(&side_effects,
                     "outcome",
                     std::string(trace_audit_event_outcome_name(event.outcome)));
  append_side_effect(&side_effects, "reason", event.reason);
  append_side_effect(&side_effects, "error_code", trace_audit_error_code_label(event));
  append_side_effect(&side_effects,
                     "exporter_state",
                     event.module_snapshot.exporter_state);
  append_side_effect(&side_effects,
                     "queue_depth",
                     std::to_string(event.module_snapshot.queue_depth));
  append_side_effect(&side_effects,
                     "dropped_total",
                     std::to_string(event.module_snapshot.dropped_total));
  append_side_effect(&side_effects,
                     "degraded",
                     event.module_snapshot.degraded ? "true" : "false");
  append_side_effect(&side_effects,
                     "current_sampler_type",
                     event.current_sampler_type);
  append_side_effect(&side_effects,
                     "previous_sampler_type",
                     event.previous_sampler_type);
  append_side_effect(&side_effects,
                     "consecutive_failure_total",
                     std::to_string(event.consecutive_failure_total));
  append_side_effect(&side_effects,
                     "degrade_enter_total",
                     std::to_string(event.degrade_enter_total));
  append_side_effect(&side_effects,
                     "recovery_success_total",
                     std::to_string(event.recovery_success_total));

  return AuditEvent{
      .event_id = options_.event_id_prefix + std::to_string(next_event_sequence_++),
      .action = std::string("tracing.") + event.action,
      .actor = event.context.worker_type,
      .target = trace_audit_target(event.kind),
      .outcome = audit_outcome_from_bridge_outcome(event.outcome),
      .evidence_ref = {
          .kind = AuditEvidenceKind::ToolResult,
          .ref = event.detail_ref,
      },
      .side_effects = std::move(side_effects),
      .timestamp = event.timestamp_ms,
  };
}

AuditContext TraceAuditBridge::make_audit_context(
    const TraceAuditEvent& event) const {
  return AuditContext{
      .request_id = event.context.infra_context.request_id,
      .session_id = event.context.infra_context.session_id,
      .trace_id = event.context.infra_context.trace_id,
      .task_id = event.context.infra_context.task_id,
      .parent_task_id = event.context.infra_context.parent_task_id,
      .lease_id = event.context.infra_context.lease_id,
      .worker_type = event.context.worker_type,
  };
}

void TraceAuditBridge::record_success(const std::string& detail_ref) {
  last_error_code_.reset();
  last_detail_ref_ = detail_ref;
}

void TraceAuditBridge::record_failure(
    std::optional<contracts::ResultCode> result_code,
    const std::string& detail_ref) {
  ++emit_failures_;
  last_error_code_ = result_code;
  last_detail_ref_ = detail_ref;
}

}  // namespace dasall::infra::tracing