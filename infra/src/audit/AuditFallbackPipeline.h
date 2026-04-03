#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "audit/AuditErrors.h"
#include "audit/AuditTypes.h"

namespace dasall::infra::audit {

struct AuditFallbackWriteResult {
  bool ok = false;
  AuditErrorCode error_code = AuditErrorCode::FallbackFail;
  std::string stage;
  std::string message;

  [[nodiscard]] static AuditFallbackWriteResult success();
  [[nodiscard]] static AuditFallbackWriteResult failure(
      AuditErrorCode error_code,
      std::string stage,
      std::string message);
};

class AuditFallbackPipeline {
 public:
  AuditFallbackPipeline(std::vector<AuditEvent>* records, std::size_t capacity)
      : records_(records), capacity_(capacity) {}

  [[nodiscard]] AuditFallbackWriteResult append(const AuditEvent& event);

 private:
  std::vector<AuditEvent>* records_ = nullptr;
  std::size_t capacity_ = 0;
};

}  // namespace dasall::infra::audit