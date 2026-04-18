#pragma once

#include "context/ContextAssemblyResult.h"
#include "context/MemoryContextRequest.h"

namespace dasall::memory {

class IContextOrchestrator {
 public:
  virtual ~IContextOrchestrator() = default;

  [[nodiscard]] virtual ContextAssemblyResult assemble(
      const MemoryContextRequest& request) = 0;
};

}  // namespace dasall::memory