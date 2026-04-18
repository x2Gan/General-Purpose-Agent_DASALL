#pragma once

#include <optional>
#include <string>
#include <vector>

#include "error/ResultCode.h"
#include "working/WorkingMemorySnapshot.h"

namespace dasall::memory {

struct WorkingMemoryExportResult {
  std::optional<contracts::ResultCode> result_code;
  WorkingMemorySnapshot snapshot;
  std::vector<std::string> warnings;
  bool degraded = false;
};

}  // namespace dasall::memory
