#pragma once

#include <optional>
#include <string>

#include "memory/SummaryMemory.h"
#include "store/StoreResult.h"

namespace dasall::memory {

class ISummaryStore {
 public:
  virtual ~ISummaryStore() = default;

  [[nodiscard]] virtual StoreResult upsert_summary(
      const contracts::SummaryMemory& summary) = 0;
  [[nodiscard]] virtual std::optional<contracts::SummaryMemory> load_latest_summary(
      const std::string& session_id) const = 0;
};

}  // namespace dasall::memory