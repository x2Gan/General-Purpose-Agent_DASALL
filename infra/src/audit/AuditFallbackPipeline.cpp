#include "AuditFallbackPipeline.h"

#include <utility>

namespace dasall::infra::audit {

AuditFallbackWriteResult AuditFallbackWriteResult::success() {
  return AuditFallbackWriteResult{
      .ok = true,
      .error_code = AuditErrorCode::FallbackFail,
      .stage = {},
      .message = {},
  };
}

AuditFallbackWriteResult AuditFallbackWriteResult::failure(
    AuditErrorCode error_code,
    std::string stage,
    std::string message) {
  return AuditFallbackWriteResult{
      .ok = false,
      .error_code = error_code,
      .stage = std::move(stage),
      .message = std::move(message),
  };
}

AuditFallbackWriteResult AuditFallbackPipeline::append(const AuditEvent& event) {
  if (records_ == nullptr) {
    return AuditFallbackWriteResult::failure(
        AuditErrorCode::FallbackFail,
        "audit.fallback.config",
        "audit fallback pipeline requires a bound degraded record store before writes can proceed");
  }

  if (capacity_ == 0) {
    return AuditFallbackWriteResult::failure(
        AuditErrorCode::FallbackFail,
        "audit.fallback.capacity",
        "audit fallback pipeline cannot append when fallback capacity is zero");
  }

  if (records_->size() >= capacity_) {
    return AuditFallbackWriteResult::failure(
        AuditErrorCode::FallbackFail,
        "audit.fallback.write",
        "audit fallback pipeline is full and cannot retain another degraded record");
  }

  records_->push_back(event);
  return AuditFallbackWriteResult::success();
}

}  // namespace dasall::infra::audit