#include "AuditValidator.h"

#include <utility>

namespace dasall::infra::audit {

AuditValidationResult AuditValidationResult::success() {
  return AuditValidationResult{
      .ok = true,
      .error_code = AuditErrorCode::InvalidEvent,
      .stage = {},
      .message = {},
  };
}

AuditValidationResult AuditValidationResult::failure(AuditErrorCode error_code,
                                                     std::string stage,
                                                     std::string message) {
  return AuditValidationResult{
      .ok = false,
      .error_code = error_code,
      .stage = std::move(stage),
      .message = std::move(message),
  };
}

AuditValidationResult AuditValidator::validate_write_input(
    const AuditEvent& event,
    const AuditContext& context) const {
  if (!event.has_required_fields()) {
    return AuditValidationResult::failure(
        AuditErrorCode::InvalidEvent,
        "audit.write.event",
        "audit event must keep identity, actor, action, target, outcome, evidence, and a positive timestamp");
  }

  if (!event.references_contract_boundary() ||
      !event.references_contract_outcome()) {
    return AuditValidationResult::failure(
        AuditErrorCode::InvalidEvent,
        "audit.write.evidence",
        "audit event evidence must stay anchored to frozen contract boundary references");
  }

  if (!event.side_effects_are_serializable()) {
    return AuditValidationResult::failure(
        AuditErrorCode::InvalidEvent,
        "audit.write.side_effects",
        "audit event side effects must stay non-empty and duplicate-free before persistence");
  }

  if (!context.has_non_empty_fields()) {
    return AuditValidationResult::failure(
        AuditErrorCode::InvalidEvent,
        "audit.write.context",
        "audit context must keep all correlation anchors non-empty or use the frozen unknown placeholder");
  }

  return AuditValidationResult::success();
}

AuditValidationResult AuditValidator::validate_export_query(
    const ExportQuery& query) const {
  if (!query.has_required_window()) {
    return AuditValidationResult::failure(
        AuditErrorCode::InvalidEvent,
        "audit.export.query",
        "audit export query requires a positive start_ts and end_ts time window");
  }

  if (!query.has_ordered_window()) {
    return AuditValidationResult::failure(
        AuditErrorCode::InvalidEvent,
        "audit.export.query",
        "audit export query requires end_ts to be greater than or equal to start_ts");
  }

  return AuditValidationResult::success();
}

}  // namespace dasall::infra::audit