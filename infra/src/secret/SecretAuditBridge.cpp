#include "SecretAuditBridge.h"

#include <chrono>
#include <string_view>
#include <utility>
#include <vector>

#include "secret/SecretErrors.h"

namespace dasall::infra::secret {
namespace {

constexpr std::string_view kSecretAuditBridgeSourceRef = "SecretAuditBridge";
constexpr std::string_view kSecretAuditStagePrefix = "secret.audit.";

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

[[nodiscard]] std::string action_token(SecretAuditAction action) {
  switch (action) {
    case SecretAuditAction::AccessGranted:
      return "access_granted";
    case SecretAuditAction::AccessDenied:
      return "access_denied";
    case SecretAuditAction::Materialized:
      return "materialized";
    case SecretAuditAction::Rotated:
      return "rotated";
    case SecretAuditAction::Revoked:
      return "revoked";
    case SecretAuditAction::Fallback:
      return "fallback";
    case SecretAuditAction::ExpiredAccess:
      return "expired_access";
    case SecretAuditAction::Unspecified:
      break;
  }

  return "invalid_action";
}

[[nodiscard]] std::string audit_action_name(SecretAuditAction action) {
  return "secret." + action_token(action);
}

[[nodiscard]] std::string make_stage(SecretAuditAction action) {
  return std::string(kSecretAuditStagePrefix) + action_token(action);
}

[[nodiscard]] AuditOutcome map_audit_outcome(const SecretAuditEvent& event) {
  switch (event.action) {
    case SecretAuditAction::AccessDenied:
      return AuditOutcome::Rejected;
    case SecretAuditAction::Fallback:
      return AuditOutcome::Escalated;
    case SecretAuditAction::AccessGranted:
    case SecretAuditAction::Materialized:
    case SecretAuditAction::Rotated:
    case SecretAuditAction::Revoked:
    case SecretAuditAction::ExpiredAccess:
      return event.outcome ? AuditOutcome::Succeeded : AuditOutcome::Failed;
    case SecretAuditAction::Unspecified:
      break;
  }

  return AuditOutcome::Unspecified;
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

SecretAuditBridge::SecretAuditBridge(std::shared_ptr<audit::IAuditLogger> audit_logger,
                                     SecretAuditBridgeOptions options)
    : audit_logger_(std::move(audit_logger)),
      options_(std::move(options)),
      last_detail_ref_(normalized_or(options_.detail_ref_prefix,
                                     "status://secret/audit/") + "idle") {
  options_.detail_ref_prefix = normalized_or(options_.detail_ref_prefix,
                                             "status://secret/audit/");
  options_.event_id_prefix = normalized_or(options_.event_id_prefix,
                                           "secret-audit-event-");
}

void SecretAuditBridge::set_audit_logger(
    std::shared_ptr<audit::IAuditLogger> audit_logger) {
  audit_logger_ = std::move(audit_logger);
}

SecretAuditEmitResult SecretAuditBridge::emit_event(SecretAuditEvent event) {
  const std::string detail_suffix = action_token(event.action);
  const std::string stage = make_stage(event.action);

  if (!event.is_valid()) {
    record_failure(contracts::ResultCode::ValidationFieldMissing,
                   detail_suffix + "/invalid_payload");
    return SecretAuditEmitResult::failure(
        {},
        {},
        make_write_failure_outcome(contracts::ResultCode::ValidationFieldMissing),
        contracts::ResultCode::ValidationFieldMissing,
        "secret audit bridge requires a valid SecretAuditEvent before emit",
        stage,
        std::string(kSecretAuditBridgeSourceRef));
  }

  if (!audit_logger_) {
    const SecretErrorMapping mapping = map_secret_error_code(SecretErrorCode::AuditWriteFail);
    record_failure(mapping.result_code,
                   detail_suffix + (options_.audit_required
                                        ? "/required_logger_unavailable"
                                        : "/logger_unavailable"));
    return SecretAuditEmitResult::failure(
        {},
        {},
        make_write_failure_outcome(mapping.result_code),
        mapping.result_code,
        std::string(secret_error_code_name(SecretErrorCode::AuditWriteFail)) +
            ": secret audit bridge requires an audit::IAuditLogger sink before emit",
        stage,
        std::string(kSecretAuditBridgeSourceRef));
  }

  AuditEvent audit_event = make_audit_event(event);
  AuditContext audit_context = make_audit_context(event);
  if (!audit_event.has_required_fields() ||
      !audit_event.side_effects_are_serializable() ||
      !audit_context.has_non_empty_fields()) {
    record_failure(contracts::ResultCode::ValidationFieldMissing,
                   detail_suffix + "/invalid_bridge_payload");
    return SecretAuditEmitResult::failure(
        std::move(audit_event),
        std::move(audit_context),
        make_write_failure_outcome(contracts::ResultCode::ValidationFieldMissing),
        contracts::ResultCode::ValidationFieldMissing,
        "secret audit bridge produced an invalid audit payload",
        stage,
        std::string(kSecretAuditBridgeSourceRef));
  }

  const AuditWriteOutcome write_outcome =
      audit_logger_->write_audit(audit_event, audit_context);
  if (!write_outcome.is_success() && !write_outcome.is_degraded_success()) {
    const SecretErrorMapping mapping = map_secret_error_code(SecretErrorCode::AuditWriteFail);
    record_failure(mapping.result_code, detail_suffix + "/write_failed");
    return SecretAuditEmitResult::failure(
        std::move(audit_event),
        std::move(audit_context),
        write_outcome,
        mapping.result_code,
        std::string(secret_error_code_name(SecretErrorCode::AuditWriteFail)) + ": " +
            describe_write_failure(write_outcome),
        stage,
        std::string(kSecretAuditBridgeSourceRef));
  }

  ++emitted_total_;
  record_success(detail_suffix + (write_outcome.is_degraded_success()
                                      ? "/degraded_success"
                                      : "/success"));
  return SecretAuditEmitResult::success(std::move(audit_event),
                                        std::move(audit_context),
                                        write_outcome);
}

SecretAuditEmitResult SecretAuditBridge::emit_access_granted(SecretAuditEvent event) {
  return emit_with_action(std::move(event), SecretAuditAction::AccessGranted);
}

SecretAuditEmitResult SecretAuditBridge::emit_access_denied(SecretAuditEvent event) {
  return emit_with_action(std::move(event), SecretAuditAction::AccessDenied);
}

SecretAuditEmitResult SecretAuditBridge::emit_rotate(SecretAuditEvent event) {
  return emit_with_action(std::move(event), SecretAuditAction::Rotated);
}

SecretAuditEmitResult SecretAuditBridge::emit_revoke(SecretAuditEvent event) {
  return emit_with_action(std::move(event), SecretAuditAction::Revoked);
}

SecretAuditEmitResult SecretAuditBridge::emit_fallback(SecretAuditEvent event) {
  return emit_with_action(std::move(event), SecretAuditAction::Fallback);
}

SecretAuditBridgeStatus SecretAuditBridge::get_status() const {
  return SecretAuditBridgeStatus{
      .emitted_total = emitted_total_,
      .emit_failures = emit_failures_,
      .degraded = emit_failures_ > 0 || last_error_code_.has_value(),
      .last_error_code = last_error_code_,
      .detail_ref = last_detail_ref_.empty() ? options_.detail_ref_prefix + "idle"
                                             : last_detail_ref_,
  };
}

AuditEvent SecretAuditBridge::make_audit_event(const SecretAuditEvent& event) {
  return AuditEvent{
      .event_id = options_.event_id_prefix + std::to_string(next_event_sequence_++),
      .action = audit_action_name(event.action),
      .actor = event.actor,
      .target = "secret:" + event.target_secret,
      .outcome = map_audit_outcome(event),
      .evidence_ref = {
          .kind = AuditEvidenceKind::ToolResult,
          .ref = event.evidence_ref,
      },
      .side_effects = std::vector<std::string>{
          make_side_effect("consumer_module", event.consumer_module),
          make_side_effect("reason_code", event.reason_code),
          make_side_effect("version", event.version),
      },
      .timestamp = current_time_unix_ms(),
  };
}

AuditContext SecretAuditBridge::make_audit_context(
    const SecretAuditEvent& event) const {
  return AuditContext{
      .request_id = event.request_id.value_or(std::string(kAuditContextUnknown)),
      .session_id = std::string(kAuditContextUnknown),
      .trace_id = std::string(kAuditContextUnknown),
      .task_id = event.task_id.value_or(std::string(kAuditContextUnknown)),
      .parent_task_id = std::string(kAuditContextUnknown),
      .lease_id = std::string(kAuditContextUnknown),
      .worker_type = event.consumer_module,
  };
}

SecretAuditEmitResult SecretAuditBridge::emit_with_action(SecretAuditEvent event,
                                                           SecretAuditAction action) {
  event.action = action;
  return emit_event(std::move(event));
}

void SecretAuditBridge::record_success(std::string detail_suffix) {
  last_error_code_.reset();
  last_detail_ref_ = options_.detail_ref_prefix + std::move(detail_suffix);
}

void SecretAuditBridge::record_failure(contracts::ResultCode result_code,
                                       std::string detail_suffix) {
  ++emit_failures_;
  last_error_code_ = result_code;
  last_detail_ref_ = options_.detail_ref_prefix + std::move(detail_suffix);
}

}  // namespace dasall::infra::secret