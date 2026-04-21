#pragma once

#include <cstdint>
#include <string>

#include "MaintenanceReport.h"
#include "config/MemoryConfig.h"
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
  virtual void run_wal_checkpoint(
      const MemoryConfig& config,
      MaintenanceReport& report) = 0;
  [[nodiscard]] virtual int run_turn_retention(
      const MemoryConfig& config,
      MaintenanceReport& report) = 0;
  [[nodiscard]] virtual int run_fact_retention(
      const MemoryConfig& config,
      MaintenanceReport& report) = 0;
  [[nodiscard]] virtual int run_experience_retention(
      const MemoryConfig& config,
      MaintenanceReport& report) = 0;
  [[nodiscard]] virtual int run_quarantine_cleanup(
      const MemoryConfig& config,
      MaintenanceReport& report) = 0;
};

}  // namespace dasall::memory