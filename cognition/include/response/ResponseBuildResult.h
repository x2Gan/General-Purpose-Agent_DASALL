#pragma once

#include <optional>
#include <string>
#include <vector>

#include "agent/AgentResult.h"
#include "error/ErrorInfo.h"
#include "error/ResultCode.h"

namespace dasall::cognition {

struct ResponseBuildResult {
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::AgentResult> agent_result;
  std::optional<contracts::ErrorInfo> error_info;
  bool fallback_used = false;
  std::vector<std::string> diagnostics;
};

}  // namespace dasall::cognition
