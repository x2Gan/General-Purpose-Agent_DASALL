#pragma once

#include <cstdint>

#include "diagnostics/CommandExecutor.h"

namespace dasall::infra::diagnostics {

class SnapshotAssembler final {
 public:
  [[nodiscard]] DiagnosticsSnapshot assemble(const DiagnosticsCommand& command,
                                             const CommandExecutionResult& execution,
                                             const EvidenceBundle& evidence);

 private:
  [[nodiscard]] std::string next_snapshot_id();

  std::uint64_t next_snapshot_index_ = 1;
};

}  // namespace dasall::infra::diagnostics