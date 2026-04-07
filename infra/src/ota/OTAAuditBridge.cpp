#include "ota/OTAAuditBridge.h"

#include <chrono>
#include <string_view>
#include <utility>
#include <vector>

#include "InfraErrorCode.h"

namespace dasall::infra::ota {
namespace {

constexpr std::string_view kOTAAuditBridgeSourceRef = "OTAAuditBridge";

[[nodiscard]] std::int64_t current_time_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] std::string normalized_or(std::string value, std::string_view fallback) {
  if (value.empty()) {
    return std::string(fallback);
  }

  return value;
}

[[nodiscard]] std::string event_token(OTAAuditBridge::EventKind kind) {
  switch (kind) {
    case OTAAuditBridge::EventKind::Precheck:
      return "precheck";
    case OTAAuditBridge::EventKind::Apply:
      return "apply";
    case OTAAuditBridge::EventKind::Rollback:
      return "rollback";
  }

  return "precheck";
}

[[nodiscard]] std::string event_action_name(OTAAuditBridge::EventKind kind) {
  return "ota." + event_token(kind);
}

[[nodiscard]] std::string event_stage(OTAAuditBridge::EventKind kind) {
  return event_action_name(kind);
}

[[nodiscard]] AuditOutcome map_outcome(OTAAuditBridge::EventKind kind, bool succeeded) {
  if (succeeded) {
    return AuditOutcome::Succeeded;
  }

  switch (kind) {
    case OTAAuditBridge::EventKind::Precheck:
      return AuditOutcome::Rejected;
    case OTAAuditBridge::EventKind::Apply:
      return AuditOutcome::Failed;
    case OTAAuditBridge::EventKind::Rollback:
      return AuditOutcome::Escalated;
  }

  return AuditOutcome::Failed;
}

[[nodiscard]] AuditEvidenceKind map_evidence_kind(OTAAuditBridge::EventKind kind) {
  return kind == OTAAuditBridge::EventKind::Rollback
             ? AuditEvidenceKind::RecoveryOutcome
             : AuditEvidenceKind::ToolResult;
}

[[nodiscard]] std::string outcome_suffix(OTAAuditBridge::EventKind kind,
                                         bool succeeded) {
  if (succeeded) {
    return "success";
  }

  switch (kind) {
    case OTAAuditBridge::EventKind::Precheck:
      return "rejected";
    case OTAAuditBridge::EventKind::Apply:
      return "failed";
    case OTAAuditBridge::EventKind::Rollback:
      return "escalated";
  }

  return "failed";
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

[[nodiscard]] std::string make_side_effect(std::string_view key,
                                           std::string_view value) {
  return std::string(key) + ":" + std::string(value);
}

[[nodiscard]] std::string describe_write_failure(const AuditWriteOutcome& write_outcome) {
  if (!write_outcome.has_consistent_state()) {
    return "audit logger returned an inconsistent write outcome";
  }

  if (write_outcome.error_code.has_value()) {
    return "audit logger returned a failing write outcome";
  }

  return "audit logger did not report success or degraded success";
}

}  // namespace

OTAAuditBridge::OTAAuditBridge(std::shared_ptr<audit::IAuditLogger> audit_logger,
                               OTAAuditBridgeOptions options)
    : audit_logger_(std::move(audit_logger)),
      options_(std::move(options)),
      last_detail_ref_(normalized_or(options_.detail_ref_prefix,
                                     "status://ota/audit/") + "idle") {
  options_.detail_ref_prefix = normalized_or(options_.detail_ref_prefix,
                                             "status://ota/audit/");
  options_.event_id_prefix = normalized_or(options_.event_id_prefix,
                                           "ota-audit-event-");
}

void OTAAuditBridge::set_audit_logger(
    std::shared_ptr<audit::IAuditLogger> audit_logger) {
  audit_logger_ = std::move(audit_logger);
}

OTAAuditEmitResult OTAAuditBridge::write_precheck_audit(OTAAuditRecord record) {
  return emit_event(std::move(record), EventKind::Precheck);
}

OTAAuditEmitResult OTAAuditBridge::write_apply_audit(OTAAuditRecord record) {
  return emit_event(std::move(record), EventKind::Apply);
}

OTAAuditEmitResult OTAAuditBridge::write_rollback_audit(OTAAuditRecord record) {
  return emit_event(std::move(record), EventKind::Rollback);
}

OTAAuditBridgeStatus OTAAuditBridge::get_status() const {
  return OTAAuditBridgeStatus{
      .emitted_total = emitted_total_,
      .emit_failures = emit_failures_,
      .degraded = emit_failures_ > 0 || last_error_code_.has_value(),
      .last_error_code = last_error_code_,
      .detail_ref = last_detail_ref_.empty() ? options_.detail_ref_prefix + "idle"
                                             : last_detail_ref_,
  };
}

OTAAuditEmitResult OTAAuditBridge::emit_event(OTAAuditRecord record, EventKind kind) {
  const std::string token = event_token(kind);
  const std::string stage = event_stage(kind);

  if (!record.is_valid()) {
    record_failure(contracts::ResultCode::ValidationFieldMissing,
                   token + "/invalid_payload");
    return OTAAuditEmitResult::failure(
        {},
        {},
        make_write_failure_outcome(contracts::ResultCode::ValidationFieldMissing),
        contracts::ResultCode::ValidationFieldMissing,
        "ota audit bridge requires a valid OTAAuditRecord before emit",
        stage,
        std::string(kOTAAuditBridgeSourceRef));
  }

  if (!audit_logger_) {
    const auto mapping = map_infra_error_code(InfraErrorCode::AuditWriteFail);
    const std::string detail_suffix = options_.audit_required
                                          ? token + "/required_logger_unavailable"
                                          : token + "/logger_unavailable";
    record_failure(mapping.result_code, detail_suffix);
    return OTAAuditEmitResult::failure(
        {},
        {},
        make_write_failure_outcome(mapping.result_code),
        mapping.result_code,
        std::string(infra_error_code_name(InfraErrorCode::AuditWriteFail)) +
            ": ota audit bridge requires an audit::IAuditLogger sink before emit",
        stage,
        std::string(kOTAAuditBridgeSourceRef));
  }

  AuditEvent audit_event = make_audit_event(record, kind);
  AuditContext audit_context = make_audit_context(record);
  if (!audit_event.has_required_fields() ||
      !audit_event.side_effects_are_serializable() ||
      !audit_context.has_non_empty_fields()) {
    record_failure(contracts::ResultCode::ValidationFieldMissing,
                   token + "/invalid_bridge_payload");
    return OTAAuditEmitResult::failure(
        std::move(audit_event),
        std::move(audit_context),
        make_write_failure_outcome(contracts::ResultCode::ValidationFieldMissing),
        contracts::ResultCode::ValidationFieldMissing,
        "ota audit bridge produced an invalid audit payload",
        stage,
        std::string(kOTAAuditBridgeSourceRef));
  }

  const AuditWriteOutcome write_outcome =
      audit_logger_->write_audit(audit_event, audit_context);
  if (!write_outcome.is_success() && !write_outcome.is_degraded_success()) {
    const auto mapping = map_infra_error_code(InfraErrorCode::AuditWriteFail);
    record_failure(mapping.result_code, token + "/write_failed");
    return OTAAuditEmitResult::failure(
        std::move(audit_event),
        std::move(audit_context),
        write_outcome,
        mapping.result_code,
        std::string(infra_error_code_name(InfraErrorCode::AuditWriteFail)) + ": " +
            describe_write_failure(write_outcome),
        stage,
        std::string(kOTAAuditBridgeSourceRef));
  }

  ++emitted_total_;
  record_success(token + "/" + outcome_suffix(kind, record.succeeded) +
                 (write_outcome.is_degraded_success() ? "/degraded_success"
                                                      : "/success"));
  return OTAAuditEmitResult::success(std::move(audit_event),
                                     std::move(audit_context),
                                     write_outcome);
}

AuditEvent OTAAuditBridge::make_audit_event(const OTAAuditRecord& record,
                                            EventKind kind) {
  return AuditEvent{
      .event_id = options_.event_id_prefix + std::to_string(next_event_sequence_++),
      .action = event_action_name(kind),
      .actor = record.actor,
      .target = "ota:" + record.target_scope,
      .outcome = map_outcome(kind, record.succeeded),
      .evidence_ref = {
          .kind = map_evidence_kind(kind),
          .ref = record.evidence_ref,
      },
      .side_effects = std::vector<std::string>{
          make_side_effect("plan_id", record.plan_id),
          make_side_effect("package_id", record.package_id),
          make_side_effect("target_scope", record.target_scope),
          make_side_effect("rollback_id", record.rollback_id),
      },
      .timestamp = current_time_unix_ms(),
  };
}

AuditContext OTAAuditBridge::make_audit_context(
    const OTAAuditRecord& record) const {
  return AuditContext{
      .request_id = record.request_id.value_or(std::string(kAuditContextUnknown)),
      .session_id = std::string(kAuditContextUnknown),
      .trace_id = record.trace_id.value_or(std::string(kAuditContextUnknown)),
      .task_id = record.task_id.value_or(std::string(kAuditContextUnknown)),
      .parent_task_id = std::string(kAuditContextUnknown),
      .lease_id = std::string(kAuditContextUnknown),
      .worker_type = std::string("ota"),
  };
}

void OTAAuditBridge::record_success(std::string detail_suffix) {
  last_error_code_.reset();
  last_detail_ref_ = options_.detail_ref_prefix + std::move(detail_suffix);
}

void OTAAuditBridge::record_failure(contracts::ResultCode result_code,
                                    std::string detail_suffix) {
  ++emit_failures_;
  last_error_code_ = result_code;
  last_detail_ref_ = options_.detail_ref_prefix + std::move(detail_suffix);
}

}  // namespace dasall::infra::ota