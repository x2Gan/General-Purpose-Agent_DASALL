#include "metrics/MetricsAuditBridge.h"

#include <chrono>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dasall::infra::metrics {
namespace {

constexpr std::string_view kMetricsAuditWorkerType = "infra.metrics";

[[nodiscard]] std::int64_t current_time_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] std::string normalized_or(std::string value,
                                        std::string_view fallback) {
  if (value.empty()) {
    return std::string(fallback);
  }

  return value;
}

[[nodiscard]] AuditOutcome audit_outcome_from_bridge_outcome(
    MetricsBridgeEventOutcome outcome) {
  switch (outcome) {
    case MetricsBridgeEventOutcome::Success:
      return AuditOutcome::Succeeded;
    case MetricsBridgeEventOutcome::Failure:
      return AuditOutcome::Failed;
    case MetricsBridgeEventOutcome::Degraded:
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
                        std::string_view key,
                        std::string value) {
  if (!value.empty()) {
    side_effects->push_back(std::string(key) + ":" + std::move(value));
  }
}

[[nodiscard]] MetricsBridgeEvent make_recovery_bridge_event(
    const MetricsRecoveryEvent& recovery_event,
    const InfraContext& context) {
  const MetricsBridgeEventOutcome outcome =
      recovery_event.error_code.has_value()
          ? MetricsBridgeEventOutcome::Degraded
          : MetricsBridgeEventOutcome::Success;

  return MetricsBridgeEvent{
      .kind = MetricsBridgeEventKind::RecoveryTransition,
      .action = recovery_event.action,
      .stage = recovery_event.stage.empty()
                   ? std::string("metrics.recovery.unknown")
                   : recovery_event.stage,
      .outcome = outcome,
      .reason = recovery_event.reason,
      .error_code = recovery_event.error_code,
      .module_snapshot = recovery_event.module_snapshot,
      .context = MetricsBridgeContext{
          .infra_context = context,
          .worker_type = std::string(kMetricsAuditWorkerType),
      },
      .detail_ref = std::string("metrics://recovery/") + recovery_event.action,
      .config_version = {},
      .previous_config_version = {},
      .consecutive_failure_total = recovery_event.consecutive_failure_total,
      .degrade_enter_total = recovery_event.degrade_enter_total,
      .recovery_success_total = recovery_event.recovery_success_total,
      .timestamp_ms = current_time_unix_ms(),
  };
}

}  // namespace

MetricsAuditBridge::MetricsAuditBridge(
    std::shared_ptr<audit::IAuditLogger> audit_logger,
    MetricsAuditBridgeOptions options)
    : audit_logger_(std::move(audit_logger)),
      options_(std::move(options)),
      last_detail_ref_(normalized_or(options_.detail_ref_prefix,
                                     "metrics://audit/") + "idle") {
  options_.detail_ref_prefix = normalized_or(options_.detail_ref_prefix,
                                             "metrics://audit/");
  options_.event_id_prefix = normalized_or(options_.event_id_prefix,
                                           "metrics-audit-event-");
}

void MetricsAuditBridge::set_audit_logger(
    std::shared_ptr<audit::IAuditLogger> audit_logger) {
  audit_logger_ = std::move(audit_logger);
}

MetricsAuditWriteResult MetricsAuditBridge::write_audit_event(
    const MetricsBridgeEvent& event) {
  const std::string detail_ref = event.detail_ref.empty()
                                     ? options_.detail_ref_prefix + "invalid_event"
                                     : event.detail_ref;
  if (!event.is_valid()) {
    record_failure(contracts::ResultCode::ValidationFieldMissing, detail_ref);
    return MetricsAuditWriteResult{
        .emitted = false,
        .audit_event = {},
        .audit_context = {},
        .write_outcome =
            make_write_failure_outcome(contracts::ResultCode::ValidationFieldMissing),
    };
  }

  if (!audit_logger_) {
    record_failure(contracts::ResultCode::RuntimeRetryExhausted, event.detail_ref);
    return MetricsAuditWriteResult{
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
    record_failure(contracts::ResultCode::ValidationFieldMissing, event.detail_ref);
    return MetricsAuditWriteResult{
        .emitted = false,
        .audit_event = std::move(audit_event),
        .audit_context = std::move(audit_context),
        .write_outcome =
            make_write_failure_outcome(contracts::ResultCode::ValidationFieldMissing),
    };
  }

  const auto write_outcome = audit_logger_->write_audit(audit_event, audit_context);
  if (!write_outcome.is_success() && !write_outcome.is_degraded_success()) {
    record_failure(write_outcome.error_code, event.detail_ref);
    return MetricsAuditWriteResult{
        .emitted = false,
        .audit_event = std::move(audit_event),
        .audit_context = std::move(audit_context),
        .write_outcome = write_outcome,
    };
  }

  ++emitted_total_;
  record_success(event.detail_ref);
  return MetricsAuditWriteResult{
      .emitted = true,
      .audit_event = std::move(audit_event),
      .audit_context = std::move(audit_context),
      .write_outcome = write_outcome,
  };
}

MetricsAuditWriteResult MetricsAuditBridge::write_recovery_event(
    const MetricsRecoveryEvent& event,
    const InfraContext& context) {
  return write_audit_event(make_recovery_bridge_event(event, context));
}

MetricsAuditBridgeStatus MetricsAuditBridge::get_status() const {
  return MetricsAuditBridgeStatus{
      .emitted_total = emitted_total_,
      .emit_failures = emit_failures_,
      .degraded = emit_failures_ > 0 || last_error_code_.has_value(),
      .last_error_code = last_error_code_,
      .detail_ref = last_detail_ref_.empty() ? options_.detail_ref_prefix + "idle"
                                             : last_detail_ref_,
  };
}

AuditEvent MetricsAuditBridge::make_audit_event(const MetricsBridgeEvent& event) {
  std::vector<std::string> side_effects;
  append_side_effect(&side_effects, "stage", event.stage);
  append_side_effect(
      &side_effects,
      "outcome",
      std::string(metrics_bridge_event_outcome_name(event.outcome)));
  append_side_effect(&side_effects, "reason", event.reason);
  append_side_effect(&side_effects,
                     "error_code",
                     event.error_code.has_value()
                         ? std::string(metrics_error_code_name(*event.error_code))
                         : std::string("none"));
  append_side_effect(&side_effects,
                     "exporter_state",
                     event.module_snapshot.exporter_state);
  append_side_effect(&side_effects,
                     "queue_depth",
                     std::to_string(event.module_snapshot.queue_depth));
  append_side_effect(&side_effects,
                     "guard_reject_total",
                     std::to_string(event.module_snapshot.guard_reject_total));
  append_side_effect(&side_effects,
                     "degraded",
                     event.module_snapshot.degraded ? "true" : "false");
  append_side_effect(&side_effects,
                     "consecutive_failure_total",
                     std::to_string(event.consecutive_failure_total));
  append_side_effect(&side_effects,
                     "degrade_enter_total",
                     std::to_string(event.degrade_enter_total));
  append_side_effect(&side_effects,
                     "recovery_success_total",
                     std::to_string(event.recovery_success_total));
  append_side_effect(&side_effects, "config_version", event.config_version);
  append_side_effect(&side_effects,
                     "previous_config_version",
                     event.previous_config_version);

  return AuditEvent{
      .event_id = options_.event_id_prefix + std::to_string(next_event_sequence_++),
      .action = std::string("metrics.") + event.action,
      .actor = event.context.worker_type,
      .target = event.kind == MetricsBridgeEventKind::RecoveryTransition
                    ? std::string("metrics:recovery")
                    : std::string("metrics:config"),
      .outcome = audit_outcome_from_bridge_outcome(event.outcome),
      .evidence_ref = {
          .kind = AuditEvidenceKind::ToolResult,
          .ref = event.detail_ref,
      },
      .side_effects = std::move(side_effects),
      .timestamp = event.timestamp_ms,
  };
}

AuditContext MetricsAuditBridge::make_audit_context(
    const MetricsBridgeEvent& event) const {
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

void MetricsAuditBridge::record_success(const std::string& detail_ref) {
  last_error_code_.reset();
  last_detail_ref_ = detail_ref;
}

void MetricsAuditBridge::record_failure(
    std::optional<contracts::ResultCode> result_code,
    const std::string& detail_ref) {
  ++emit_failures_;
  last_error_code_ = result_code;
  last_detail_ref_ = detail_ref;
}

}  // namespace dasall::infra::metrics