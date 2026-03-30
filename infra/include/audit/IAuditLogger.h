#pragma once

#include "audit/AuditExporterTypes.h"
#include "audit/AuditTypes.h"

namespace dasall::infra::audit {

class IAuditLogger {
 public:
  virtual ~IAuditLogger() = default;

  virtual AuditWriteOutcome write_audit(const AuditEvent& event,
                                        const AuditContext& context) = 0;
  virtual ExportResult export_audit(const ExportQuery& query) = 0;
};

}  // namespace dasall::infra::audit