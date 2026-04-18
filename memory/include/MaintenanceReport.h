#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dasall::memory {

struct MaintenanceReport {
  bool checkpoint_executed = false;
  int checkpoint_wal_pages_remaining = 0;
  int turns_purged = 0;
  int facts_purged = 0;
  int experiences_purged = 0;
  int quarantine_cleaned = 0;
  bool vector_rebuild_executed = false;
  std::vector<std::string> warnings;
  std::int64_t duration_ms = 0;
};

}  // namespace dasall::memory
