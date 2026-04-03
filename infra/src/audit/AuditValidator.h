#pragma once

#include <string>

#include "audit/AuditErrors.h"
#include "audit/AuditExporterTypes.h"
#include "audit/AuditTypes.h"

namespace dasall::infra::audit {

struct AuditValidationResult {
  bool ok = false;
  AuditErrorCode error_code = AuditErrorCode::InvalidEvent;
  std::string stage;
  std::string message;

  [[nodiscard]] static AuditValidationResult success();
  [[nodiscard]] static AuditValidationResult failure(AuditErrorCode error_code,
                                                     std::string stage,
                                                     std::string message);

  [[nodiscard]] bool has_observable_failure() const {
    return !ok && !stage.empty() && !message.empty();
  }
};

class AuditValidator {
 public:
  [[nodiscard]] AuditValidationResult validate_write_input(
      const AuditEvent& event,
      const AuditContext& context) const;

  [[nodiscard]] AuditValidationResult validate_export_query(
      const ExportQuery& query) const;
};

}  // namespace dasall::infra::audit