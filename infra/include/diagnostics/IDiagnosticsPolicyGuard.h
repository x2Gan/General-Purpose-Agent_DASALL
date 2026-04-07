#pragma once

#include "InfraContext.h"
#include "diagnostics/DiagnosticsTypes.h"

namespace dasall::infra::diagnostics {

class IDiagnosticsPolicyGuard {
 public:
  virtual ~IDiagnosticsPolicyGuard() = default;

  [[nodiscard]] virtual CommandDecision authorize(const DiagnosticsCommand& command,
                                                  const InfraContext& context) = 0;
};

}  // namespace dasall::infra::diagnostics