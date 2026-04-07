#pragma once

#include "diagnostics/CommandExecutor.h"

namespace dasall::infra::diagnostics {

class EvidenceCollector final {
 public:
  [[nodiscard]] EvidenceBundle collect(const DiagnosticsCommand& command,
                                       const CommandExecutionResult& execution) const;
};

}  // namespace dasall::infra::diagnostics