#include "LLMAuditBridge.h"

#include <string>
#include <utility>
#include <vector>

namespace dasall::llm::observability {
namespace {

[[nodiscard]] infra::AuditOutcome audit_outcome(LLMAuditEventKind kind) {
  switch (kind) {
    case LLMAuditEventKind::TrustedSourceFailure:
      return infra::AuditOutcome::Rejected;
    case LLMAuditEventKind::ReasoningContentStripped:
    case LLMAuditEventKind::MetadataDrift:
      return infra::AuditOutcome::Escalated;
  }

  return infra::AuditOutcome::Failed;
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

void append_side_effect(std::vector<std::string>* side_effects,
                        std::string_view key,
                        std::string value) {
  if (!value.empty()) {
    side_effects->push_back(std::string(key) + ":" + std::move(value));
  }
}

[[nodiscard]] std::string audit_target(LLMAuditEventKind kind) {
  switch (kind) {
    case LLMAuditEventKind::TrustedSourceFailure:
      return "llm:prompt";
    case LLMAuditEventKind::ReasoningContentStripped:
      return "llm:response";
    case LLMAuditEventKind::MetadataDrift:
      return "llm:provider_metadata";
  }

  return "llm:unknown";
}

}  // namespace

bool LLMAuditContext::is_valid() const {
  return !infra_context.request_id.empty() && !infra_context.session_id.empty() &&
         !infra_context.trace_id.empty() && !infra_context.task_id.empty() &&
         !infra_context.parent_task_id.empty() && !infra_context.lease_id.empty() &&
         !worker_type.empty();
}

bool LLMAuditEvent::has_consistent_values() const {
  if (stage.empty() || reason.empty() || detail_ref.empty() || llm_call_id.empty() ||
      prompt_id.empty() || prompt_version.empty() || resolved_route.empty() ||
      model_name.empty() || timestamp_ms <= 0 || !context.is_valid()) {
    return false;
  }

  switch (kind) {
    case LLMAuditEventKind::TrustedSourceFailure:
      return !trusted_source.empty() && metadata_field.empty() &&
             expected_value.empty() && observed_value.empty();
    case LLMAuditEventKind::ReasoningContentStripped:
      return !reasoning_mode_requested.empty() &&
             !reasoning_mode_effective.empty() && trusted_source.empty() &&
             metadata_field.empty() && expected_value.empty() && observed_value.empty();
    case LLMAuditEventKind::MetadataDrift:
      return trusted_source.empty() && !metadata_field.empty() &&
             !expected_value.empty() && !observed_value.empty();
  }

  return false;
}

bool LLMAuditBridgeStatus::is_valid() const {
  if (detail_ref.empty()) {
    return false;
  }

  if (last_error_code.has_value() &&
      contracts::classify_result_code(*last_error_code) ==
          contracts::ResultCodeCategory::Unknown) {
    return false;
  }

  return true;
}

LLMAuditBridge::LLMAuditBridge(
    std::shared_ptr<infra::audit::IAuditLogger> audit_logger)
    : audit_logger_(std::move(audit_logger)) {}

void LLMAuditBridge::set_audit_logger(
    std::shared_ptr<infra::audit::IAuditLogger> audit_logger) {
  audit_logger_ = std::move(audit_logger);
}

LLMAuditWriteResult LLMAuditBridge::write_audit_event(const LLMAuditEvent& event) {
  const std::string detail_ref = event.detail_ref.empty()
                                     ? std::string(kLLMAuditDefaultDetailRef)
                                     : event.detail_ref;
  if (!event.has_consistent_values()) {
    record_failure(contracts::ResultCode::ValidationFieldMissing, detail_ref);
    return LLMAuditWriteResult{
        .emitted = false,
        .audit_event = {},
        .audit_context = {},
        .write_outcome =
            make_write_failure_outcome(contracts::ResultCode::ValidationFieldMissing),
    };
  }

  if (!audit_logger_) {
    record_failure(contracts::ResultCode::RuntimeRetryExhausted, detail_ref);
    return LLMAuditWriteResult{
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
    return LLMAuditWriteResult{
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
    return LLMAuditWriteResult{
        .emitted = false,
        .audit_event = audit_event,
        .audit_context = audit_context,
        .write_outcome = write_outcome,
    };
  }

  record_success(detail_ref);
  return LLMAuditWriteResult{
      .emitted = true,
      .audit_event = audit_event,
      .audit_context = audit_context,
      .write_outcome = write_outcome,
  };
}

LLMAuditBridgeStatus LLMAuditBridge::get_status() const {
  return LLMAuditBridgeStatus{
      .emitted_total = emitted_total_,
      .emit_failures = emit_failures_,
      .degraded = emit_failures_ > 0U || last_error_code_.has_value(),
      .last_error_code = last_error_code_,
      .detail_ref = last_detail_ref_.empty() ? std::string(kLLMAuditDefaultDetailRef)
                                             : last_detail_ref_,
  };
}

infra::AuditEvent LLMAuditBridge::make_audit_event(const LLMAuditEvent& event) {
  std::vector<std::string> side_effects;
  append_side_effect(&side_effects, "stage", event.stage);
  append_side_effect(&side_effects, "llm_call_id", event.llm_call_id);
  append_side_effect(&side_effects, "prompt_id", event.prompt_id);
  append_side_effect(&side_effects, "prompt_version", event.prompt_version);
  append_side_effect(&side_effects, "resolved_route", event.resolved_route);
  append_side_effect(&side_effects, "model_name", event.model_name);
  append_side_effect(&side_effects, "profile_id", event.profile_id);
  append_side_effect(&side_effects, "reason", event.reason);

  switch (event.kind) {
    case LLMAuditEventKind::TrustedSourceFailure:
      append_side_effect(&side_effects, "trusted_source", event.trusted_source);
      break;
    case LLMAuditEventKind::ReasoningContentStripped:
      append_side_effect(&side_effects,
                         "reasoning_mode_requested",
                         event.reasoning_mode_requested);
      append_side_effect(&side_effects,
                         "reasoning_mode_effective",
                         event.reasoning_mode_effective);
      break;
    case LLMAuditEventKind::MetadataDrift:
      append_side_effect(&side_effects, "metadata_field", event.metadata_field);
      append_side_effect(&side_effects, "expected_value", event.expected_value);
      append_side_effect(&side_effects, "observed_value", event.observed_value);
      break;
  }

  return infra::AuditEvent{
      .event_id = std::string("llm-audit-event-") +
                  std::to_string(next_event_sequence_++),
      .action = std::string("llm.") + std::string(llm_audit_action_name(event.kind)),
      .actor = event.context.worker_type,
      .target = audit_target(event.kind),
      .outcome = audit_outcome(event.kind),
      .evidence_ref = {
          .kind = infra::AuditEvidenceKind::WorkerTask,
          .ref = event.detail_ref,
      },
      .side_effects = std::move(side_effects),
      .timestamp = event.timestamp_ms,
  };
}

infra::AuditContext LLMAuditBridge::make_audit_context(
    const LLMAuditEvent& event) const {
  return infra::AuditContext{
      .request_id = event.context.infra_context.request_id,
      .session_id = event.context.infra_context.session_id,
      .trace_id = event.context.infra_context.trace_id,
      .task_id = event.context.infra_context.task_id,
      .parent_task_id = event.context.infra_context.parent_task_id,
      .lease_id = event.context.infra_context.lease_id,
      .worker_type = event.context.worker_type,
  };
}

void LLMAuditBridge::record_success(const std::string& detail_ref) {
  ++emitted_total_;
  last_error_code_.reset();
  last_detail_ref_ = detail_ref;
}

void LLMAuditBridge::record_failure(
    std::optional<contracts::ResultCode> result_code,
    const std::string& detail_ref) {
  ++emit_failures_;
  last_error_code_ = result_code;
  last_detail_ref_ = detail_ref;
}

}  // namespace dasall::llm::observability