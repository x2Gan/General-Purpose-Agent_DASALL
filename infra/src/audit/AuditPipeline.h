#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "audit/AuditErrors.h"
#include "audit/AuditTypes.h"

namespace dasall::infra::audit {

struct AuditPipelineWriteResult {
  bool ok = false;
  AuditErrorCode error_code = AuditErrorCode::WriteFail;
  std::string stage;
  std::string message;

  [[nodiscard]] static AuditPipelineWriteResult success();
  [[nodiscard]] static AuditPipelineWriteResult failure(AuditErrorCode error_code,
                                                        std::string stage,
                                                        std::string message);
};

class AuditPipeline {
 public:
  AuditPipeline(std::vector<AuditEvent>* records, std::size_t capacity)
      : records_(records), capacity_(capacity) {}

  [[nodiscard]] AuditPipelineWriteResult append(const AuditEvent& event);

 private:
  std::vector<AuditEvent>* records_ = nullptr;
  std::size_t capacity_ = 0;
};

}  // namespace dasall::infra::audit