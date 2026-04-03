#include "AuditLinkAdapter.h"

#include <string>

namespace dasall::infra::logging {

namespace {

constexpr std::string_view kAuditLinkAdapterSourceRef = "AuditLinkAdapter";

}  // namespace

LogWriteResult AuditLinkAdapter::attach_audit_ref(LogEvent& event,
                                                  const AuditRef& audit_ref) {
  if (!event.attrs_are_serializable()) {
    report_link_failure("log event attrs must remain serializable before audit linking");
    return LogWriteResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "audit link adapter requires serializable log attrs",
        "logging.attach_audit_ref",
        std::string(kAuditLinkAdapterSourceRef));
  }

  if (!is_high_risk_event(event)) {
    return LogWriteResult::success();
  }

  if (!audit_ref.has_value() || !audit_ref.has_non_empty_fields() ||
      audit_ref.uses_unknown_defaults()) {
    report_link_failure("high-risk log requires evidence_ref, trace_id and task_id");
    return LogWriteResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "high-risk log requires a complete audit reference",
        "logging.attach_audit_ref",
        std::string(kAuditLinkAdapterSourceRef));
  }

  event.attrs.insert_or_assign("audit_ref_pending", "true");
  event.attrs.insert_or_assign("evidence_ref", audit_ref.evidence_ref.ref);
  event.attrs.insert_or_assign("evidence_kind",
                               evidence_kind_name(audit_ref.evidence_ref.kind));
  event.attrs.insert_or_assign("audit_trace_id", audit_ref.trace_id);
  event.attrs.insert_or_assign("audit_task_id", audit_ref.task_id);
  return LogWriteResult::success();
}

void AuditLinkAdapter::report_link_failure(std::string_view reason) {
  ++link_failure_count_;
  last_failure_reason_ = std::string(reason);
}

bool AuditLinkAdapter::is_high_risk_event(const LogEvent& event) {
  return event.level == LogLevel::Error || event.level == LogLevel::Fatal ||
         event.category() == "audit";
}

std::string AuditLinkAdapter::evidence_kind_name(AuditEvidenceKind kind) {
  switch (kind) {
    case AuditEvidenceKind::ToolResult:
      return "tool_result";
    case AuditEvidenceKind::RecoveryOutcome:
      return "recovery_outcome";
    case AuditEvidenceKind::WorkerTask:
      return "worker_task";
    case AuditEvidenceKind::Unspecified:
      break;
  }

  return "unknown";
}

}  // namespace dasall::infra::logging