#pragma once

#include <cstdint>
#include <string>

#include "store/StoreResult.h"

namespace dasall::memory {

class IMaintenanceStore {
 public:
  virtual ~IMaintenanceStore() = default;

  [[nodiscard]] virtual std::int64_t count_turns(const std::string& session_id) const = 0;
  [[nodiscard]] virtual StoreResult quarantine_record(
      const std::string& object_type,
      const std::string& object_id,
      const std::string& reason) = 0;
};

}  // namespace dasall::memory