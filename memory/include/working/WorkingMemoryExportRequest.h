#pragma once

#include <string>

namespace dasall::memory {

struct WorkingMemoryExportRequest {
  std::string session_id;
  std::string export_reason = "manual";
  bool include_ephemeral_facts = false;
  bool evict_expired_before_export = false;
};

}  // namespace dasall::memory
