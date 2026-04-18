#pragma once

#include <string>
#include <vector>

namespace dasall::memory {

struct SummaryProjection {
  std::string summary_text;
  std::vector<std::string> decisions_made;
  std::vector<std::string> confirmed_facts;
  std::vector<std::string> tool_outcomes;
  std::vector<std::string> source_turn_ids;
  int estimated_tokens = 0;
};

}  // namespace dasall::memory