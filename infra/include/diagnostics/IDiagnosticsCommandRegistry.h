#pragma once

#include "diagnostics/DiagnosticsTypes.h"

namespace dasall::infra::diagnostics {

class IDiagnosticsCommandRegistry {
 public:
  virtual ~IDiagnosticsCommandRegistry() = default;

  [[nodiscard]] virtual CommandCatalog list_commands() = 0;
  [[nodiscard]] virtual ValidationResult validate(const DiagnosticsCommand& command) = 0;
};

}  // namespace dasall::infra::diagnostics