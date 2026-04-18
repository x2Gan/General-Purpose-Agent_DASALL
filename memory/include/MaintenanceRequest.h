#pragma once

namespace dasall::memory {

struct MaintenanceRequest {
  bool run_checkpoint = true;
  bool run_retention = true;
  bool run_quarantine_cleanup = true;
  bool run_vector_rebuild = false;
};

}  // namespace dasall::memory
