#pragma once

#include <optional>

#include "IExperienceStore.h"
#include "IFactStore.h"
#include "IMaintenanceStore.h"
#include "ISessionStore.h"
#include "ISummaryStore.h"
#include "ITransactionalStore.h"
#include "config/MemoryConfig.h"

namespace dasall::memory {

class IMemoryStore : public ITransactionalStore,
                     public ISessionStore,
                     public ISummaryStore,
                     public IFactStore,
                     public IExperienceStore,
                     public IMaintenanceStore {
 public:
  virtual ~IMemoryStore() = default;

  [[nodiscard]] virtual std::optional<contracts::ResultCode> open(
      const MemoryConfig& config) = 0;
  virtual void close() noexcept = 0;
};

}  // namespace dasall::memory