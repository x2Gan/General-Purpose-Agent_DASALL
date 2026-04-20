#pragma once

#include <optional>
#include <string>
#include <vector>

#include "error/ResultCode.h"
#include "writeback/SummaryProjection.h"

namespace dasall::memory {

struct SummaryGenerationResult {
  SummaryProjection projection;
  std::vector<std::string> warnings;
  bool fallback_used = false;
  bool degraded = false;
  std::optional<contracts::ResultCode> result_code;
};

}  // namespace dasall::memory