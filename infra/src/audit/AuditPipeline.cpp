#include "AuditPipeline.h"

#include <utility>

namespace dasall::infra::audit {

AuditPipelineWriteResult AuditPipelineWriteResult::success() {
  return AuditPipelineWriteResult{
      .ok = true,
      .error_code = AuditErrorCode::WriteFail,
      .stage = {},
      .message = {},
  };
}

AuditPipelineWriteResult AuditPipelineWriteResult::failure(
    AuditErrorCode error_code,
    std::string stage,
    std::string message) {
  return AuditPipelineWriteResult{
      .ok = false,
      .error_code = error_code,
      .stage = std::move(stage),
      .message = std::move(message),
  };
}

AuditPipelineWriteResult AuditPipeline::append(const AuditEvent& event) {
  if (records_ == nullptr) {
    return AuditPipelineWriteResult::failure(
        AuditErrorCode::WriteFail,
        "audit.pipeline.config",
        "audit pipeline requires a bound append-only record store before writes can proceed");
  }

  if (capacity_ == 0) {
    return AuditPipelineWriteResult::failure(
        AuditErrorCode::WriteFail,
        "audit.pipeline.capacity",
        "audit pipeline cannot append when primary capacity is zero");
  }

  if (records_->size() >= capacity_) {
    return AuditPipelineWriteResult::failure(
        AuditErrorCode::WriteFail,
        "audit.pipeline.write",
        "audit pipeline append-only store is full and cannot accept another event");
  }

  records_->push_back(event);
  return AuditPipelineWriteResult::success();
}

}  // namespace dasall::infra::audit