#pragma once

#include <string>
#include <vector>

#include "writeback/SummaryProjection.h"

namespace dasall::memory {

struct SummaryGenerationResult {
  SummaryProjection projection;
  std::vector<std::string> warnings;
  bool fallback_used = false;
  bool degraded = false;
};

}  // namespace dasall::memory