#pragma once

#include <string>

namespace dasall::memory {

struct MaintenanceRequest {
  bool run_checkpoint = true;
  bool run_retention = true;
  bool run_quarantine_cleanup = true;
  bool run_vector_rebuild = false;
  std::string request_id;
  std::string trace_id;
};

}  // namespace dasall::memory
