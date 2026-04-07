#include "plugin/PluginAuditAdapter.h"

#include <chrono>
#include <string_view>
#include <utility>
#include <vector>

#include "InfraErrorCode.h"

namespace dasall::infra::plugin {
namespace {

constexpr std::string_view kPluginAuditAdapterSourceRef = "PluginAuditAdapter";

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

[[nodiscard]] std::string event_token(PluginAuditAdapter::EventKind kind) {
  switch (kind) {
    case PluginAuditAdapter::EventKind::Load:
      return "load";
    case PluginAuditAdapter::EventKind::Unload:
      return "unload";
    case PluginAuditAdapter::EventKind::PolicyDeny:
      return "policy_deny";
  }

  return "load";
}

[[nodiscard]] std::string event_action_name(PluginAuditAdapter::EventKind kind) {
  return "plugin." + event_token(kind);
}

[[nodiscard]] AuditOutcome map_outcome(PluginAuditAdapter::EventKind kind,
                                       bool succeeded) {
  if (kind == PluginAuditAdapter::EventKind::PolicyDeny) {
    return AuditOutcome::Rejected;
  }

  return succeeded ? AuditOutcome::Succeeded : AuditOutcome::Failed;
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

[[nodiscard]] std::string describe_write_failure(
    const AuditWriteOutcome& write_outcome) {
  if (!write_outcome.has_consistent_state()) {
    return "audit logger returned an inconsistent write outcome";
  }

  if (write_outcome.error_code.has_value()) {
    return "audit logger returned a failing write outcome";
  }

  return "audit logger did not report success or degraded success";
}

[[nodiscard]] std::string result_code_name(contracts::ResultCode result_code) {
  switch (result_code) {
    case contracts::ResultCode::ValidationFieldMissing:
      return "ValidationFieldMissing";
    case contracts::ResultCode::PolicyDenied:
      return "PolicyDenied";
    case contracts::ResultCode::ToolExecutionFailed:
      return "ToolExecutionFailed";
    case contracts::ResultCode::ProviderTimeout:
      return "ProviderTimeout";
    case contracts::ResultCode::RuntimeRetryExhausted:
      return "RuntimeRetryExhausted";
  }

  return "Unknown";
}

[[nodiscard]] std::string make_side_effect(std::string_view key,
                                           std::string_view value) {
  return std::string(key) + ":" + std::string(value);
}

}  // namespace

PluginAuditAdapter::PluginAuditAdapter(
    std::shared_ptr<audit::IAuditLogger> audit_logger,
    PluginAuditAdapterOptions options)
    : audit_logger_(std::move(audit_logger)),
      options_(std::move(options)),
      last_detail_ref_(normalized_or(options_.detail_ref_prefix,
                                     "status://plugin/audit/") + "idle") {
  options_.detail_ref_prefix = normalized_or(options_.detail_ref_prefix,
                                             "status://plugin/audit/");
  options_.event_id_prefix = normalized_or(options_.event_id_prefix,
                                           "plugin-audit-event-");
}

void PluginAuditAdapter::set_audit_logger(
    std::shared_ptr<audit::IAuditLogger> audit_logger) {
  audit_logger_ = std::move(audit_logger);
}

PluginAuditEmitResult PluginAuditAdapter::write_load_audit(PluginAuditRecord record) {
  return emit_event(std::move(record), EventKind::Load);
}

PluginAuditEmitResult PluginAuditAdapter::write_unload_audit(PluginAuditRecord record) {
  return emit_event(std::move(record), EventKind::Unload);
}

PluginAuditEmitResult PluginAuditAdapter::write_policy_deny_audit(
    PluginAuditRecord record) {
  return emit_event(std::move(record), EventKind::PolicyDeny);
}

PluginAuditAdapterStatus PluginAuditAdapter::get_status() const {
  return PluginAuditAdapterStatus{
      .emitted_total = emitted_total_,
      .emit_failures = emit_failures_,
      .degraded = emit_failures_ > 0 || last_error_code_.has_value(),
      .last_error_code = last_error_code_,
      .detail_ref = last_detail_ref_.empty() ? options_.detail_ref_prefix + "idle"
                                             : last_detail_ref_,
  };
}

PluginAuditEmitResult PluginAuditAdapter::emit_event(PluginAuditRecord record,
                                                      EventKind kind) {
  const std::string token = event_token(kind);
  const std::string stage = event_action_name(kind);

  if (!record.is_valid()) {
    record_failure(contracts::ResultCode::ValidationFieldMissing,
                   token + "/invalid_payload");
    return PluginAuditEmitResult::failure(
        {},
        {},
        make_write_failure_outcome(contracts::ResultCode::ValidationFieldMissing),
        contracts::ResultCode::ValidationFieldMissing,
        "plugin audit adapter requires a valid PluginAuditRecord before emit",
        stage,
        std::string(kPluginAuditAdapterSourceRef));
  }

  if (!audit_logger_) {
    const auto mapping = map_infra_error_code(InfraErrorCode::AuditWriteFail);
    const std::string detail_suffix = options_.audit_required
                                          ? token + "/required_logger_unavailable"
                                          : token + "/logger_unavailable";
    record_failure(mapping.result_code, detail_suffix);
    return PluginAuditEmitResult::failure(
        {},
        {},
        make_write_failure_outcome(mapping.result_code),
        mapping.result_code,
        std::string(infra_error_code_name(InfraErrorCode::AuditWriteFail)) +
            ": plugin audit adapter requires an audit::IAuditLogger sink before emit",
        stage,
        std::string(kPluginAuditAdapterSourceRef));
  }

  AuditEvent audit_event = make_audit_event(record, kind);
  AuditContext audit_context = make_audit_context(record);
  if (!audit_event.has_required_fields() ||
      !audit_event.side_effects_are_serializable() ||
      !audit_context.has_non_empty_fields()) {
    record_failure(contracts::ResultCode::ValidationFieldMissing,
                   token + "/invalid_bridge_payload");
    return PluginAuditEmitResult::failure(
        std::move(audit_event),
        std::move(audit_context),
        make_write_failure_outcome(contracts::ResultCode::ValidationFieldMissing),
        contracts::ResultCode::ValidationFieldMissing,
        "plugin audit adapter produced an invalid audit payload",
        stage,
        std::string(kPluginAuditAdapterSourceRef));
  }

  const auto write_outcome = audit_logger_->write_audit(audit_event, audit_context);
  if (!write_outcome.is_success() && !write_outcome.is_degraded_success()) {
    const auto mapping = map_infra_error_code(InfraErrorCode::AuditWriteFail);
    record_failure(mapping.result_code, token + "/write_failed");
    return PluginAuditEmitResult::failure(
        std::move(audit_event),
        std::move(audit_context),
        write_outcome,
        mapping.result_code,
        std::string(infra_error_code_name(InfraErrorCode::AuditWriteFail)) + ": " +
            describe_write_failure(write_outcome),
        stage,
        std::string(kPluginAuditAdapterSourceRef));
  }

  ++emitted_total_;
  record_success(token +
                 (kind == EventKind::PolicyDeny
                      ? "/rejected"
                      : (record.succeeded ? "/success" : "/failed")));
  return PluginAuditEmitResult::success(std::move(audit_event),
                                        std::move(audit_context),
                                        write_outcome);
}

AuditEvent PluginAuditAdapter::make_audit_event(const PluginAuditRecord& record,
                                                EventKind kind) {
  std::vector<std::string> side_effects{
      make_side_effect("reason_code", record.reason_code),
  };
  if (record.result_code.has_value()) {
    side_effects.push_back(
        make_side_effect("result_code", result_code_name(*record.result_code)));
  }

  return AuditEvent{
      .event_id = options_.event_id_prefix + std::to_string(next_event_sequence_++),
      .action = event_action_name(kind),
      .actor = record.actor_ref,
      .target = std::string("plugin:") + record.plugin_id,
      .outcome = map_outcome(kind, record.succeeded),
      .evidence_ref = {
          .kind = AuditEvidenceKind::ToolResult,
          .ref = record.evidence_ref,
      },
      .side_effects = std::move(side_effects),
      .timestamp = current_time_unix_ms(),
  };
}

AuditContext PluginAuditAdapter::make_audit_context(
    const PluginAuditRecord& record) const {
  return AuditContext{
      .request_id = record.request_id.value_or(std::string(kAuditContextUnknown)),
      .session_id = std::string(kAuditContextUnknown),
      .trace_id = record.trace_id.value_or(std::string(kAuditContextUnknown)),
      .task_id = record.task_id.value_or(std::string(kAuditContextUnknown)),
      .parent_task_id = std::string(kAuditContextUnknown),
      .lease_id = std::string(kAuditContextUnknown),
      .worker_type = std::string("plugin"),
  };
}

void PluginAuditAdapter::record_success(std::string detail_suffix) {
  last_error_code_.reset();
  last_detail_ref_ = options_.detail_ref_prefix + std::move(detail_suffix);
}

void PluginAuditAdapter::record_failure(contracts::ResultCode result_code,
                                        std::string detail_suffix) {
  ++emit_failures_;
  last_error_code_ = result_code;
  last_detail_ref_ = options_.detail_ref_prefix + std::move(detail_suffix);
}

}  // namespace dasall::infra::plugin