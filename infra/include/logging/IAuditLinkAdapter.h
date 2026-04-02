#pragma once

#include <string_view>

#include "logging/ILogger.h"

namespace dasall::infra::logging {

struct AuditRef;

class IAuditLinkAdapter {
 public:
  virtual ~IAuditLinkAdapter() = default;

  virtual LogWriteResult attach_audit_ref(LogEvent& event,
                                          const AuditRef& audit_ref) = 0;
  virtual void report_link_failure(std::string_view reason) = 0;
};

}  // namespace dasall::infra::logging