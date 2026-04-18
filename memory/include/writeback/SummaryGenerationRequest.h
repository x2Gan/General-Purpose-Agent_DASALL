#pragma once

#include <optional>
#include <string>
#include <vector>

#include "memory/SummaryMemory.h"
#include "memory/Turn.h"

namespace dasall::memory {

struct SummaryGenerationRequest {
  std::string session_id;
  std::vector<contracts::Turn> source_turns;
  std::optional<contracts::SummaryMemory> existing_summary;
  int target_token_budget = 0;
  std::string strategy_hint;
};

}  // namespace dasall::memory