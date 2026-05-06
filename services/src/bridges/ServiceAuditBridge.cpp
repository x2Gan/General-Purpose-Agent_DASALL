#include "bridges/ServiceAuditBridge.h"

#include <algorithm>
#include <chrono>
#include <string_view>
#include <utility>

namespace dasall::services::internal {

namespace {

constexpr std::string_view kServiceAuditBridgeSourceRef = "ServiceAuditBridge";

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

[[nodiscard]] std::string effective_result_code_name(
    const std::optional<contracts::ResultCode>& result_code,
    const std::optional<contracts::ErrorInfo>& error) {
  const auto effective_code = service_result_effective_failure_code(result_code, error);
  return effective_code.has_value() ? result_code_name(*effective_code) : std::string("Unknown");
}

[[nodiscard]] std::string describe_write_failure(
    const infra::AuditWriteOutcome& write_outcome) {
  if (!write_outcome.has_consistent_state()) {
    return "service audit logger returned an inconsistent write outcome";
  }

  if (write_outcome.error_code.has_value()) {
    return "service audit logger returned a failing write outcome";
  }

  return "service audit logger did not report success or degraded success";
}

void append_unique_side_effect(std::vector<std::string>* side_effects,
                               std::string value) {
  if (value.empty()) {
    return;
  }

  if (std::find(side_effects->begin(), side_effects->end(), value) ==
      side_effects->end()) {
    side_effects->push_back(std::move(value));
  }
}

void append_key_value_side_effect(std::vector<std::string>* side_effects,
                                  std::string_view key,
                                  const std::string& value) {
  if (!value.empty()) {
    append_unique_side_effect(side_effects,
                              std::string(key) + ":" + value);
  }
}

void append_bool_side_effect(std::vector<std::string>* side_effects,
                             std::string_view key,
                             bool value) {
  append_unique_side_effect(side_effects,
                            std::string(key) + ":" +
                                (value ? "true" : "false"));
}

[[nodiscard]] std::string make_tool_call_actor(const ServiceCallContext& context) {
  return context.tool_call_id.empty() ? std::string("tool_call://unknown")
                                      : std::string("tool_call://") +
                                            context.tool_call_id;
}

[[nodiscard]] std::string make_target_ref(const CapabilityTargetRef& target) {
  return target.capability_id.empty() ? target.target_id
                                      : target.capability_id + ":" +
                                            target.target_id;
}

[[nodiscard]] infra::AuditOutcome outcome_for_command_result(
    const ExecutionCommandResult& result) {
  if (!result.error.has_value()) {
    return infra::AuditOutcome::Succeeded;
  }

  if (result.error->failure_type.has_value() &&
      *result.error->failure_type == contracts::ResultCodeCategory::Policy) {
    return infra::AuditOutcome::Rejected;
  }

  return infra::AuditOutcome::Failed;
}

void append_result_side_effects(std::vector<std::string>* side_effects,
                                const ExecutionCommandResult& result) {
  for (const auto& side_effect : result.side_effects) {
    append_unique_side_effect(side_effects, side_effect);
  }

  for (const auto& compensation_hint : result.compensation_hints) {
    append_key_value_side_effect(side_effects, "compensation_hint",
                                 compensation_hint);
  }

  if (!result.error.has_value()) {
    append_key_value_side_effect(side_effects, "result_code",
                                 std::string("none"));
    return;
  }

  append_key_value_side_effect(side_effects, "result_code",
                               effective_result_code_name(result.code, result.error));
  append_key_value_side_effect(side_effects, "error_stage",
                               result.error->details.stage);
  append_key_value_side_effect(side_effects, "error_ref",
                               result.error->source_ref.ref_id);
}

[[nodiscard]] infra::AuditWriteOutcome make_write_failure_outcome(
    contracts::ResultCode result_code) {
  return infra::AuditWriteOutcome{
      .accepted = false,
      .persisted = false,
      .fallback_used = false,
      .error_code = result_code,
  };
}

}  // namespace

ServiceAuditBridge::ServiceAuditBridge(infra::audit::IAuditLogger* audit_logger,
                                       ServiceAuditBridgeOptions options)
    : audit_logger_(audit_logger),
      options_(std::move(options)),
      last_detail_ref_(normalized_or(options_.detail_ref_prefix,
                                     "status://services/audit/") +
                       "idle") {
  options_.detail_ref_prefix = normalized_or(options_.detail_ref_prefix,
                                             "status://services/audit/");
  options_.event_id_prefix = normalized_or(options_.event_id_prefix,
                                           "services-audit-event-");
  options_.worker_type = normalized_or(options_.worker_type,
                                       kServiceAuditDefaultWorkerType);
}

void ServiceAuditBridge::set_audit_logger(infra::audit::IAuditLogger* audit_logger) {
  audit_logger_ = audit_logger;
}

ServiceAuditEmitResult ServiceAuditBridge::write_execution_requested(
    const ServiceCallContext& context,
    const CapabilityTargetRef& target,
    std::string action,
    std::string execution_id,
    bool require_confirmation) {
  std::vector<std::string> side_effects;
  append_key_value_side_effect(&side_effects, "operation", action);
  append_key_value_side_effect(&side_effects, "capability_id", target.capability_id);
  append_key_value_side_effect(&side_effects, "execution_id", execution_id);
  append_key_value_side_effect(&side_effects, "request_id", context.request_id);
  append_bool_side_effect(&side_effects, "require_confirmation", require_confirmation);

  const auto detail_ref = execution_id.empty() ? std::string("execution://unknown")
                                               : std::string("execution://") + execution_id;
  return emit_event(ServiceAuditEventKind::execution_requested,
                    context,
                    target,
                    std::move(action),
                    infra::AuditOutcome::Escalated,
                    infra::AuditEvidenceKind::ToolResult,
                    detail_ref,
                    detail_ref,
                    std::move(side_effects));
}

ServiceAuditEmitResult ServiceAuditBridge::write_execution_completed(
    const ServiceCallContext& context,
    const CapabilityTargetRef& target,
    std::string action,
    const ExecutionCommandResult& result) {
  std::vector<std::string> side_effects;
  append_key_value_side_effect(&side_effects, "operation", action);
  append_key_value_side_effect(&side_effects, "capability_id", target.capability_id);
  append_key_value_side_effect(&side_effects, "execution_id", result.execution_id);
  append_result_side_effects(&side_effects, result);

  const auto detail_ref = result.execution_id.empty()
                              ? std::string("execution://unknown")
                              : std::string("execution://") + result.execution_id;
  return emit_event(ServiceAuditEventKind::execution_completed,
                    context,
                    target,
                    std::move(action),
                    outcome_for_command_result(result),
                    infra::AuditEvidenceKind::ToolResult,
                    detail_ref,
                    detail_ref,
                    std::move(side_effects));
}

ServiceAuditEmitResult ServiceAuditBridge::write_compensation_requested(
    const ServiceCallContext& context,
    const ExecutionCompensationRequest& request,
    std::string compensation_execution_id) {
  std::vector<std::string> side_effects;
  append_key_value_side_effect(&side_effects, "operation",
                               request.compensation_action);
  append_key_value_side_effect(&side_effects, "capability_id",
                               request.target.capability_id);
  append_key_value_side_effect(&side_effects, "compensation_execution_id",
                               compensation_execution_id);
  append_key_value_side_effect(&side_effects, "source_execution_id",
                               request.source_execution_id);
  append_key_value_side_effect(&side_effects, "reason_code",
                               request.reason_code);

  const auto detail_ref = compensation_execution_id.empty()
                              ? std::string("compensation://unknown")
                              : std::string("compensation://") +
                                    compensation_execution_id;
  return emit_event(ServiceAuditEventKind::compensation_requested,
                    context,
                    request.target,
                    request.compensation_action,
                    infra::AuditOutcome::Escalated,
                    infra::AuditEvidenceKind::RecoveryOutcome,
                    detail_ref,
                    detail_ref,
                    std::move(side_effects));
}

ServiceAuditEmitResult ServiceAuditBridge::write_compensation_completed(
    const ServiceCallContext& context,
    const ExecutionCompensationRequest& request,
    const ExecutionCommandResult& result) {
  std::vector<std::string> side_effects;
  append_key_value_side_effect(&side_effects, "operation",
                               request.compensation_action);
  append_key_value_side_effect(&side_effects, "capability_id",
                               request.target.capability_id);
  append_key_value_side_effect(&side_effects, "compensation_execution_id",
                               result.execution_id);
  append_key_value_side_effect(&side_effects, "source_execution_id",
                               request.source_execution_id);
  append_key_value_side_effect(&side_effects, "reason_code",
                               request.reason_code);
  append_result_side_effects(&side_effects, result);

  const auto detail_ref = result.execution_id.empty()
                              ? std::string("compensation://unknown")
                              : std::string("compensation://") +
                                    result.execution_id;
  return emit_event(ServiceAuditEventKind::compensation_completed,
                    context,
                    request.target,
                    request.compensation_action,
                    outcome_for_command_result(result),
                    infra::AuditEvidenceKind::RecoveryOutcome,
                    detail_ref,
                    detail_ref,
                    std::move(side_effects));
}

ServiceAuditEmitResult ServiceAuditBridge::write_fallback_blocked(
    const ServiceCallContext& context,
    const CapabilityTargetRef& target,
    std::string action,
    std::string execution_id,
    std::string deny_reason,
    std::string action_class) {
  std::vector<std::string> side_effects;
  append_key_value_side_effect(&side_effects, "operation", action);
  append_key_value_side_effect(&side_effects, "capability_id", target.capability_id);
  append_key_value_side_effect(&side_effects, "execution_id", execution_id);
  append_key_value_side_effect(&side_effects, "deny_reason", deny_reason);
  append_key_value_side_effect(&side_effects, "action_class", action_class);
  append_key_value_side_effect(&side_effects, "result_code",
                               std::string("RuntimeRetryExhausted"));

  const auto detail_ref = context.request_id.empty()
                              ? std::string("route://unknown")
                              : std::string("route://") + context.request_id;
  return emit_event(ServiceAuditEventKind::fallback_blocked,
                    context,
                    target,
                    std::move(action),
                    infra::AuditOutcome::Rejected,
                    infra::AuditEvidenceKind::ToolResult,
                    detail_ref,
                    detail_ref,
                    std::move(side_effects));
}

ServiceAuditBridgeStatus ServiceAuditBridge::get_status() const {
  return ServiceAuditBridgeStatus{
      .emitted_total = emitted_total_,
      .emit_failures = emit_failures_,
      .degraded = emit_failures_ > 0 || last_error_code_.has_value(),
      .last_error_code = last_error_code_,
      .detail_ref = last_detail_ref_.empty() ? options_.detail_ref_prefix + "idle"
                                             : last_detail_ref_,
  };
}

ServiceAuditEmitResult ServiceAuditBridge::emit_event(
    ServiceAuditEventKind kind,
    const ServiceCallContext& context,
    const CapabilityTargetRef& target,
    std::string action,
    infra::AuditOutcome outcome,
    infra::AuditEvidenceKind evidence_kind,
    std::string evidence_ref,
    std::string detail_ref,
    std::vector<std::string> side_effects) {
  if (detail_ref.empty()) {
    detail_ref = options_.detail_ref_prefix + "invalid_event";
  }

  if (!audit_logger_) {
    record_failure(contracts::ResultCode::RuntimeRetryExhausted, detail_ref);
    return ServiceAuditEmitResult::failure(
        {},
        {},
        make_write_failure_outcome(contracts::ResultCode::RuntimeRetryExhausted),
        contracts::ResultCode::RuntimeRetryExhausted,
        "service audit bridge requires an infra::audit::IAuditLogger sink before audit-required events can proceed",
        "services.audit",
        std::string(kServiceAuditBridgeSourceRef));
  }

  append_key_value_side_effect(&side_effects, "action", action);

  auto audit_event = make_audit_event(kind,
                                      context,
                                      target,
                                      outcome,
                                      evidence_kind,
                                      std::move(evidence_ref),
                                      std::move(side_effects),
                                      current_time_unix_ms());
  auto audit_context = make_audit_context(context);
  if (!audit_event.has_required_fields() ||
      !audit_event.side_effects_are_serializable() ||
      !audit_context.has_non_empty_fields()) {
    record_failure(contracts::ResultCode::ValidationFieldMissing, detail_ref);
    return ServiceAuditEmitResult::failure(
        std::move(audit_event),
        std::move(audit_context),
        make_write_failure_outcome(contracts::ResultCode::ValidationFieldMissing),
        contracts::ResultCode::ValidationFieldMissing,
        "service audit bridge produced an invalid audit payload",
        "services.audit",
        std::string(kServiceAuditBridgeSourceRef));
  }

  const auto write_outcome = audit_logger_->write_audit(audit_event, audit_context);
  if (!write_outcome.is_success() && !write_outcome.is_degraded_success()) {
    const auto result_code =
        write_outcome.error_code.value_or(contracts::ResultCode::RuntimeRetryExhausted);
    record_failure(result_code, detail_ref);
    return ServiceAuditEmitResult::failure(
        std::move(audit_event),
        std::move(audit_context),
        write_outcome,
        result_code,
        describe_write_failure(write_outcome),
        "services.audit",
        std::string(kServiceAuditBridgeSourceRef));
  }

  ++emitted_total_;
  record_success(detail_ref);
  return ServiceAuditEmitResult::success(std::move(audit_event),
                                         std::move(audit_context),
                                         write_outcome);
}

infra::AuditEvent ServiceAuditBridge::make_audit_event(
    ServiceAuditEventKind kind,
    const ServiceCallContext& context,
    const CapabilityTargetRef& target,
    infra::AuditOutcome outcome,
    infra::AuditEvidenceKind evidence_kind,
    std::string evidence_ref,
    std::vector<std::string> side_effects,
    std::int64_t timestamp_ms) {
  return infra::AuditEvent{
      .event_id = options_.event_id_prefix + std::to_string(next_event_sequence_++),
      .action = std::string(service_audit_event_name(kind)),
      .actor = make_tool_call_actor(context),
      .target = make_target_ref(target),
      .outcome = outcome,
      .evidence_ref = {
          .kind = evidence_kind,
          .ref = std::move(evidence_ref),
      },
      .side_effects = std::move(side_effects),
      .timestamp = timestamp_ms,
  };
}

infra::AuditContext ServiceAuditBridge::make_audit_context(
    const ServiceCallContext& context) const {
  return infra::AuditContext{
      .request_id = normalized_or(context.request_id, infra::kAuditContextUnknown),
      .session_id = normalized_or(context.session_id, infra::kAuditContextUnknown),
      .trace_id = normalized_or(context.trace_id, infra::kAuditContextUnknown),
      .task_id = normalized_or(context.goal_id, infra::kAuditContextUnknown),
      .parent_task_id = std::string(infra::kAuditContextUnknown),
      .lease_id = std::string(infra::kAuditContextUnknown),
      .worker_type = options_.worker_type,
  };
}

void ServiceAuditBridge::record_success(const std::string& detail_ref) {
  last_error_code_.reset();
  last_detail_ref_ = detail_ref;
}

void ServiceAuditBridge::record_failure(
    std::optional<contracts::ResultCode> result_code,
    const std::string& detail_ref) {
  ++emit_failures_;
  last_error_code_ = result_code;
  last_detail_ref_ = detail_ref;
}

}  // namespace dasall::services::internal